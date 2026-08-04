// stl includes
#include <cassert>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// libMesh includes
#include "libmesh/elem.h"
#include "libmesh/mesh.h"
#include "libmesh/node.h"

// xdg includes
#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "xdg/embree/ray_tracer.h"

// for pseudo-transport testing
#include "particle_sim.h"

// for connectivity testing
#include "util.h"

using namespace xdg;
using namespace xdg::test;

TEST_CASE("Test Brick")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<LibMeshManager>()};

  mesh_manager->load_file("brick.exo");

  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 1);
}


TEST_CASE("Test Brick w/ Sidesets")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<LibMeshManager>()};
  mesh_manager->load_file("brick-sidesets.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);
}

TEST_CASE("Test BVH Build Brick")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<LibMeshManager>();

  mesh_manager->load_file("brick.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 1);

  std::unique_ptr<RayTracer> ray_tracing_interface = std::make_unique<EmbreeRayTracer>();
  for (auto volume : mesh_manager->volumes()) {
    ray_tracing_interface->register_volume(mesh_manager, volume);
  }

  // volume elements will be detected on the libmesh mesh, so three trees will be registered
  REQUIRE(ray_tracing_interface->num_registered_trees() == 3);
}

TEST_CASE("Test BVH Build Brick w/ Sidesets")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<LibMeshManager>();
  mesh_manager->load_file("brick-sidesets.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);

  std::unique_ptr<RayTracer> ray_tracing_interface = std::make_unique<EmbreeRayTracer>();

  for (auto volume : mesh_manager->volumes()) {
    ray_tracing_interface->register_volume(mesh_manager, volume);
  }
  // volume elements will be detected on the libmesh mesh, so two trees will be registered
  REQUIRE(ray_tracing_interface->num_registered_trees() == 3);
}

TEST_CASE("Test Ray Fire Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};
  std::pair<double, MeshID> intersection;

  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));
}

TEST_CASE("Test Cylinder-Brick Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<LibMeshManager>()};

  mesh_manager->load_file("cyl-brick.exo");

  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 3);

  REQUIRE(mesh_manager->num_surfaces() == 12);

  // get an element from each volume and check its volume ID
  auto vol1_elems = mesh_manager->get_volume_elements(1);
  REQUIRE(!vol1_elems.empty());

  auto vol2_elems = mesh_manager->get_volume_elements(2);
  REQUIRE(!vol2_elems.empty());

  mesh_manager->parse_metadata();

  xdg::Property prop;

  // check the model's metadata
  prop = mesh_manager->get_volume_property(1, PropertyType::MATERIAL);
  REQUIRE(prop.type == PropertyType::MATERIAL);
  REQUIRE(prop.value == "steel");

  prop = mesh_manager->get_volume_property(2, PropertyType::MATERIAL);
  REQUIRE(prop.type == PropertyType::MATERIAL);
  REQUIRE(prop.value == "iron");

  for (auto s : mesh_manager->surfaces()) {
    prop = mesh_manager->get_surface_property(s, PropertyType::BOUNDARY_CONDITION);
    std::cout << s << ", " << prop.value << std::endl;
    REQUIRE(prop.type == PropertyType::BOUNDARY_CONDITION);
    if (s == 3) {
      REQUIRE(prop.value == "transmission");
    } else if (s == 4) {
      REQUIRE(prop.value == "reflective");
    } else {
      REQUIRE(prop.value == "vacuum");
    }
  }
}

TEST_CASE("Test Ray Fire Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 2;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 10.0};
  Direction direction {0.0, 0.0, 1.0};
  std::pair<double, MeshID> intersection;
  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire ray in the opposite direction
  direction = {0.0, 0.0, -1.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire ray from the center of the cylinder in the negative z direction
  volume = 1;
  origin = {0.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire a ray from the center of the cylinder in the positive z direction
  direction = {0.0, 0.0, 1.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire a ray from the center of the cylinder in the positive x direction
  direction = {1.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-3));

  volume = 1;
  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));

  volume = 2;
  origin = {0.0, 0.0, 10.0};
  REQUIRE(xdg->point_in_volume(volume, origin));
}

TEST_CASE("Test Ray Fire Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  int n_rays {1000};

  for (int i = 0; i < n_rays; i++) {
    direction = rand_dir();
    std::pair<double, MeshID> intersection;
    intersection = xdg->ray_fire(volume, origin, direction);
    if (intersection.second == ID_NONE)
      fatal_error("Ray did not intersect any geometry");
    if (intersection.first > 6.4) {
      fatal_error("Ray intersected geometry at distance greater than 6.4 cm");
    }
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(6.3849, 1e-1));
  }
}

TEST_CASE("Test Volume Element Count Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  auto elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 10333);
  REQUIRE(mesh_manager->num_volume_elements() == 10333);
}

TEST_CASE("Test Point Location Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  // the origin of the problem should be in the volume
  MeshID volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == volume);

  // a point outside of the sphere should be in the implicit complement
  origin = {0.0, 0.0, 10.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == xdg->mesh_manager()->implicit_complement());
}

TEST_CASE("Test Hex Point Location Jezebel Quads")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel-quads.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  Position inside {0.0, 0.0, 0.0};
  MeshID expected_id = 33243;
  REQUIRE(xdg->find_element(inside) == expected_id);

  Position outside {0.0, 0.0, 100.0};
  REQUIRE(xdg->find_element(outside) == ID_NONE);
}

TEST_CASE("Test Hex Element Walk Jezebel Quads")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel-quads.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  Position inside {0.1, 0.1, 0.1};
  MeshID start_element = xdg->find_element(inside);
  REQUIRE(start_element != ID_NONE);

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  Direction u;
  do {
    u = {dist(gen), dist(gen), dist(gen)};
  } while (u.length() == 0.0);
  u.normalize();

  double distance = mesh_manager->global_bounding_box().max_chord_length() * 0.1;
  if (distance <= 0.0) distance = 1.0;

  auto segments = mesh_manager->walk_elements(start_element, inside, u, distance);
  REQUIRE_FALSE(segments.empty());
  REQUIRE(segments.front().first == start_element);

  double total_length = std::accumulate(segments.begin(), segments.end(), 0.0,
    [](double sum, const std::pair<MeshID, double>& segment) {
      return sum + segment.second;
    });
  REQUIRE(total_length >= 0.0);
  REQUIRE(total_length <= distance + 1e-6);
}

TEST_CASE("Test Point Location Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  REQUIRE(mesh_manager->num_volume_elements() == 16624);

  MeshID expected_volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  // test a point inside the cylinder
  MeshID volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == expected_volume);

  // test a point inside the cube
  expected_volume = 2;
  origin = {0.0, 0.0, 10.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == expected_volume);

  // a point outside of the sphere should be in the implicit complement
  origin = {0.0, 0.0, 100.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == xdg->mesh_manager()->implicit_complement());
}

TEST_CASE("Test Volume Element Count Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;
  auto elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 7587);

  volume = 2;
  elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 9037);
}

TEST_CASE("Test Find Element Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  REQUIRE(mesh_manager->num_volume_elements() == 8790);

  MeshID volume = 1;

  MeshID element = xdg->find_element(volume, {0.0, 0.0, 0.0});
  REQUIRE(element != ID_NONE);

  element = xdg->find_element(volume, {0.0, 0.0, 100.0});
  REQUIRE(element == ID_NONE);
}

TEST_CASE("Test Track Exiting Mesh Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;
  Position start {0.0, 0.0, -1000.0};
  Position end {0.0, 0.0, 1000.0};
  auto tracks = xdg->segments(volume, start, end);

  double length = std::accumulate(tracks.begin(), tracks.end(), 0.0, [](double sum, const auto& track) {
    return sum + track.second;
  });

  REQUIRE_THAT(length, Catch::Matchers::WithinAbs(10.0, 1e-6));
}

TEST_CASE("LibMesh Element ID and Index Mapping")
{
  // test mapping for contiguous IDs
  {
    std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<LibMeshManager>()};
    REQUIRE(mesh_manager->mesh_library() == MeshLibrary::LIBMESH);
    mesh_manager->load_file("jezebel.exo");
    mesh_manager->init();
    REQUIRE(mesh_manager->num_volume_elements() == 10333);

    size_t num_elements = mesh_manager->num_volume_elements();
    for (size_t i = 0; i < num_elements; i++) {
      MeshID element_id = mesh_manager->element_id(i);
      int element_index = mesh_manager->element_index(element_id);
      REQUIRE(element_index == static_cast<int>(i));
    }

    size_t num_vertices = mesh_manager->num_vertices();
    REQUIRE(num_vertices == 2067);
    for (size_t i = 0; i < num_vertices; i++) {
      MeshID vertex_id = mesh_manager->vertex_id(i);
      int vertex_index = mesh_manager->vertex_index(vertex_id);
      REQUIRE(vertex_index == static_cast<int>(i));
    }
  }

  // test mapping for non-contiguous IDs via manual modification
  {
    // create libMesh mesh object and load the mesh
    std::unique_ptr<libMesh::Mesh> mesh = std::make_unique<libMesh::Mesh>(*XDGConfig::config().libmesh_comm(), 3);
    mesh->read("jezebel.exo");

    // now create a new mesh manager (create explicitly so we can modify the mesh before init)
    std::unique_ptr<LibMeshManager> mesh_manager = std::make_unique<LibMeshManager>(mesh.get());

    // get the last element and vertex IDs before modification
    MeshID last_element_id = mesh->elem_ptr(mesh->n_elem() - 1)->id();
    MeshID last_vertex_id = mesh->node_ptr(mesh->n_nodes() - 1)->id();

    // add some new elements to the mesh to create gaps in the element ID space
    MeshID next_id = last_vertex_id + 1;

    std::vector<MeshID> modified_vertex_ids;
    std::unordered_map<MeshID, MeshID> node_renumbering;
    for (int i = 0; i <= last_vertex_id; i++) {
      auto* node = mesh->node_ptr(i);
      // create a gap every 15 vertices
      if (node->id() % 15 == 0) {
        next_id += 3;
      } else {
        next_id++;
      }

      std::unique_ptr<libMesh::Node> new_node = std::make_unique<libMesh::Node>(*node, next_id);
      mesh->add_node(std::move(new_node));
      node_renumbering[node->id()] = next_id;
      modified_vertex_ids.push_back(next_id);
    }

    next_id = last_element_id + 1;
    std::vector<MeshID> modified_element_ids;
    std::vector<std::pair<MeshID, MeshID>> element_renumbering;
    for (int i = 0; i <= last_element_id; i++) {
      auto* elem = mesh->elem_ptr(i);
      // create a gap every 100 elements
      if (elem->id() % 100 == 0) {
        next_id += 3;
      } else {
        next_id++;
      }
      modified_element_ids.push_back(next_id);
      // create a new element with the next ID and the same nodes as the current element
      std::unique_ptr<libMesh::Elem> new_elem = libMesh::Elem::build_with_id(elem->type(), next_id);
      auto* new_elem_ptr = mesh->add_elem(std::move(new_elem));
      for (unsigned int j = 0; j < elem->n_nodes(); j++) {
        MeshID node_id = elem->node_ptr(j)->id();
        new_elem_ptr->set_node(j) = mesh->node_ptr(node_renumbering.at(node_id));
      }
      mesh->delete_elem(elem);
    }

    // delete all original nodes from the mesh
    for (int i = 0; i <= last_vertex_id; i++) {
      mesh->delete_node(mesh->node_ptr(i));
    }

    // keep libMesh from renumbering the elements when initializing the mesh
    // manager for the purposes of this test
    mesh->allow_renumbering(false);
    mesh->prepare_for_use();

    // ensure that the resulting ID spaces really are non-zero and non-contiguous
    REQUIRE(modified_element_ids.size() == 10333);
    REQUIRE(modified_vertex_ids.size() == 2067);

    MeshID prev_elem_id = modified_element_ids.front() - 1;
    int n_gaps = 0;
    for (auto elem : mesh->active_element_ptr_range()) {
      REQUIRE(elem->id() > prev_elem_id);
      if (elem->id() - prev_elem_id > 1) {
        n_gaps++;
      }
    }
    if (n_gaps == 0) FAIL("Element IDs are still contiguous after modification");

    MeshID prev_node_id = modified_vertex_ids.front() - 1;
    int n_node_gaps = 0;
    for (auto node : mesh->node_ptr_range()) {
      REQUIRE(node->id() > prev_node_id);
      if (node->id() - prev_node_id > 1) {
        n_node_gaps++;
      }
      prev_node_id = node->id();
    }

    if (n_node_gaps == 0) FAIL("Node IDs are still contiguous after modification");

    // now initialize the mesh manager
    mesh_manager->init();
    REQUIRE(mesh_manager->num_volume_elements() == 10333);

      // check element ID to index mapping
    int expected_index = 0;
    for (const auto* elem : mesh_manager->mesh()->active_element_ptr_range()) {
      MeshID element_id = elem->id();
      int element_index = mesh_manager->element_index(element_id);
      REQUIRE(element_index == expected_index);
      expected_index++;
    }

    // check node ID to index mapping
    expected_index = 0;
    for (const auto* node : mesh_manager->mesh()->node_ptr_range()) {
      MeshID vertex_id = node->id();
      int vertex_index = mesh_manager->vertex_index(vertex_id);
      REQUIRE(vertex_index == expected_index);
      expected_index++;
    }

    // check index to element ID mapping
    size_t num_elements = mesh_manager->num_volume_elements();
    for (size_t i = 0; i < num_elements; i++) {
      MeshID element_id = mesh_manager->element_id(i);
      REQUIRE(element_id == modified_element_ids[i]);
    }

    // check index to vertex ID mapping
    size_t num_vertices = mesh_manager->mesh()->n_nodes();
    for (size_t i = 0; i < num_vertices; i++) {
       MeshID vertex_id = mesh_manager->vertex_id(i);
       REQUIRE(vertex_id == modified_vertex_ids[i]);
    }
  }
}

TEST_CASE("Multiblock sidesets")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube-w-multiblock-sideset.exo");
  mesh_manager->init();
  mesh_manager->parse_metadata();

  xdg->prepare_raytracer();

  MeshID volume = 1;
  Position start {0.0, 0.0, 0.0};
  Position end {15.0, 20.0, 3.0};
  auto tracks = xdg->segments(volume, start, end);

  assert(tracks.size() > 0);
}

TEMPLATE_TEST_CASE("TEST libMesh Raytrace Quads", "[libMesh][faces][quads]",
                   Embree_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel-quads.exo");
    mesh_manager->init();
    xdg->prepare_raytracer();

    // should have two volumes including the implicit complement
    REQUIRE(mesh_manager->num_volumes() == 2);
    REQUIRE(mesh_manager->num_volume_faces(1) == 2400);
    REQUIRE(mesh_manager->num_surfaces() == 1);
    REQUIRE(mesh_manager->num_surface_faces(1) == 2400);

    MeshID volume = 1;
    Position origin {0.0, 0.0, 0.0};
    Direction dir {1.0, 0.0, 0.0};

    // fire rays in random directions and verify that we get a hit at
    // approximately the expected distance of 6.3849 (the radius of the sphere)
    for (int i = 0; i < 10; ++i) {
      dir = rand_dir();
      auto hit = xdg->ray_fire(volume, origin, dir);
      REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(6.3849, 1e-2));
      REQUIRE(hit.second != ID_NONE);
    }
  }
}

struct JezebelExo { static constexpr std::string_view filename = "jezebel.exo"; };
struct JezebelQuadsExo { static constexpr std::string_view filename = "jezebel-quads.exo"; };
struct CylBrickExo { static constexpr std::string_view filename = "cyl-brick.exo"; };
struct CylBrickQuadsExo { static constexpr std::string_view filename = "cyl-brick-quads.exo"; };

TEMPLATE_TEST_CASE("Test libMesh Transport", "[libmesh][transport]",
                   JezebelExo,
                   JezebelQuadsExo,
                   CylBrickExo,
                   CylBrickQuadsExo)
{
  std::string filename {TestType::filename};

  DYNAMIC_SECTION("Model: " << filename) {
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::LIBMESH);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(filename);
    mesh_manager->parse_metadata();
    mesh_manager->init();
    xdg->prepare_raytracer();

    SimulationData sim_data;
    sim_data.xdg_ = xdg;
    sim_data.n_particles_ = 1000;
    sim_data.mfp_ = 0.5;
    sim_data.verbose_particles_ = false;
    sim_data.implicit_complement_is_graveyard_ = true;
    transport_particles(sim_data);
  }
}
