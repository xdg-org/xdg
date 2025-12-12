// stl includes
#include <iostream>
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "xdg/embree/ray_tracer.h"

using namespace xdg;

void print_intersection(std::pair<double, MeshID> intersection) {
  std::cout << "Intersection: " << intersection.first << " " << intersection.second << std::endl;
}

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

  // now create a new mesh manager
  std::unique_ptr<LibMeshManager> mesh_manager2  {std::make_unique<LibMeshManager>()};
  mesh_manager2->load_file("jezebel.exo");

  // tweak some of the element IDs to create gaps
  auto* mesh = mesh_manager2->mesh();
  int next_id = 0;
  for (auto* elem : mesh->active_element_ptr_range()) {
    // create a gap every 10 elements
    if (elem->id() % 10 == 0) {
      next_id += 5;
    } else {
      next_id++;
    }
    elem->set_id() = next_id; // create a gap every 10 elements
    REQUIRE(elem->id() == next_id);
  }
  mesh->allow_renumbering(false);

  // now initialize the mesh manager
  mesh_manager2->init();
  REQUIRE(mesh_manager2->num_volume_elements() == 10333);

  num_elements = mesh_manager2->num_volume_elements();
  int expected_index = 0;
  for (const auto* elem : mesh_manager2->mesh()->active_element_ptr_range()) {
    MeshID element_id = elem->id();
    int element_index = mesh_manager2->element_index(element_id);
    REQUIRE(element_index == expected_index);
    expected_index++;
  }
}