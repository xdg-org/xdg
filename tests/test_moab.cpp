// stl includes
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// MOAB includes
#include "moab/Interface.hpp"
#include "moab/Range.hpp"
#include "moab/Types.hpp"

// xdg includes
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/xdg.h"

#include "util.h"
#include "particle_sim.h"

using namespace xdg;
using namespace xdg::test;

TEST_CASE("Test MOAB Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MOABMeshManager>();

  mesh_manager->load_file("pwr_pincell.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 4);
  REQUIRE(mesh_manager->num_surfaces() == 12);

  // create the implicit complement volume
  mesh_manager->create_implicit_complement();
  REQUIRE(mesh_manager->num_volumes() == 5);

  // parse metadata
  mesh_manager->parse_metadata();

  std::map<MeshID, std::string> material_exp_results =
    {
      {1, "UO2 (2.4%)"},
      {2, "Zircaloy"},
      {3, "Hot borated water"},
      {4, "void"},
      {5, "void"}
    };

  for (auto volume : mesh_manager->volumes()) {
    auto prop = mesh_manager->get_volume_property(volume, PropertyType::MATERIAL);
    REQUIRE(prop.type == PropertyType::MATERIAL);
    REQUIRE(material_exp_results[volume] == prop.value);
  }

  std::vector reflecting_surface_ids {2, 3, 14, 15, 17, 18};
  for (auto surface : reflecting_surface_ids) {
    auto prop = mesh_manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION);
    REQUIRE(prop.value == "reflecting");
  }

  // none of the volumes in this model should contain volumetric elements
  for (auto volume : mesh_manager->volumes()) {
    REQUIRE(mesh_manager->num_volume_elements(volume) == 0);
  }
}

TEMPLATE_TEST_CASE("Test BVH Build", "[moab][bvh]",
                   Embree_Raytracer,
                   GPRT_Raytracer)
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MOABMeshManager>();

  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);

  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);

    for (const auto& volume : mesh_manager->volumes()) {
      rti->register_volume(mesh_manager, volume);
    }
    REQUIRE(rti->num_registered_trees() == 2);
  }
}


TEMPLATE_TEST_CASE("Test Ray Fire MOAB (all built backends)", "[ray_tracer][moab]",
                   Embree_Raytracer,
                   GPRT_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);

    const auto& mm = xdg->mesh_manager();
    mm->load_file("cube.h5m");
    mm->init();
    xdg->prepare_raytracer();

    MeshID volume = mm->volumes()[0];

    Position origin {0.0, 0.0, 0.0};
    Direction dir {1.0, 0.0, 0.0};

    auto hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    origin = {3.0, 0.0, 0.0};
    hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

    origin = {-10.0, 0.0, 0.0};
    hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(15.0, 1e-6));

    origin = {0.0, 0.0, 0.0};
    REQUIRE(xdg->point_in_volume(volume, origin));
  }
}

TEST_CASE("MOAB Element Types")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  for (const auto surface: mesh_manager->surfaces()) {
    auto element_type = mesh_manager->get_surface_face_type(surface);
    REQUIRE(element_type == SurfaceFaceType::TRI); // Each surface should be made up of triangles
  }
}

TEMPLATE_TEST_CASE("TEST MOAB Find Element Method", "[moab][elements]",
                   Embree_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel.h5m");
    mesh_manager->init();
    xdg->prepare_raytracer();

    MeshID volume = 1;
    REQUIRE(mesh_manager->num_volume_elements(1) == 10333);

    MeshID element = xdg->find_element(volume, {0.0, 0.0, 100.0});
    REQUIRE(element == ID_NONE); // should not find an element since the point is outside the volume

    element = xdg->find_element(volume, {0.0, 0.0, 0.0});
    REQUIRE(element != ID_NONE); // should find an element

    // test the next_element method
    auto next_element = xdg->mesh_manager()->next_element(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    REQUIRE(next_element.first != ID_NONE);
    REQUIRE(next_element.second != INFTY);

    // test the walk_elements method
    auto walk_elements = xdg->mesh_manager()->walk_elements(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 100.0);
    // get the sum of the distances
    double distance = std::accumulate(walk_elements.begin(), walk_elements.end(), 0.0,
                                      [](double total, const auto& segment) { return total + segment.second; });
    REQUIRE(distance > 0.0);
    REQUIRE(distance <= 100.0);
    for (const auto& segment : walk_elements) {
      REQUIRE(segment.first != ID_NONE);
      REQUIRE(segment.second >= 0.0);
    }
  }
}

TEMPLATE_TEST_CASE("TEST MOAB Raytrace Quads", "[moab][faces][quads]",
                   Embree_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel-quads.h5m");
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

TEST_CASE("MOAB Multi-volume quad raytrace")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick-quads.h5m");
  mesh_manager->init();
  xdg->prepare_raytracer();

  // should have two volumes including the implicit complement
  REQUIRE(mesh_manager->num_volumes() == 3);
  REQUIRE(mesh_manager->num_surfaces() == 12);

  MeshID volume = 1;
  Position origin {0.0, 0.0, 0.0};
  Direction dir {0.0, 0.0, -1.0};

  auto hit = xdg->ray_fire(volume, origin, dir);
  REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(7.5, 1e-6));
  REQUIRE(hit.second != ID_NONE);

  dir = {0.0, 0.0, 1.0};
  hit = xdg->ray_fire(volume, origin, dir);
  REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(7.5, 1e-6));
  REQUIRE(hit.second != ID_NONE);
}


struct JezebelH5M {
  static constexpr std::string_view filename = "jezebel.h5m";
  static constexpr int32_t num_elements = 10333;
  static constexpr int32_t num_faces = 1276;
  static constexpr int32_t num_vertices = 2067;
};
struct JezebelQuadsH5M {
  static constexpr std::string_view filename = "jezebel-quads.h5m";
  static constexpr int32_t num_elements = 36800;
  static constexpr int32_t num_faces = 2415;
  static constexpr int32_t num_vertices = 38085;
};
struct CylBrickH5M {
  static constexpr std::string_view filename = "cyl-brick.h5m";
  static constexpr int32_t num_elements = 0;
  static constexpr int32_t num_faces = 2474;
  static constexpr int32_t num_vertices = 1580;
};
struct CylBrickQuadsH5M {
  static constexpr std::string_view filename = "cyl-brick-quads.h5m";
  static constexpr int32_t num_elements = 0;
  static constexpr int32_t num_faces = 20478;
  static constexpr int32_t num_vertices = 196869;
};

TEMPLATE_TEST_CASE("MOAB Element ID and Index Mapping", "[moab][mapping]",
                   JezebelH5M,
                   JezebelQuadsH5M)
{
  std::string filename {TestType::filename};

  // test mapping for contiguous MOAB IDs using jezebel model
  DYNAMIC_SECTION("Model: " << filename) {
    {
      std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
      REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
      const auto& mesh_manager = xdg->mesh_manager();
      mesh_manager->load_file(filename);
      mesh_manager->init();

      size_t num_elements = mesh_manager->num_volume_elements();
      REQUIRE(num_elements == TestType::num_elements);

      for (size_t idx = 0; idx < num_elements; ++idx) {
        MeshID element_id = mesh_manager->element_id(idx);
        // MOAB element IDs start at 1 and, for this model, are contiguous
        REQUIRE(element_id == idx + 1);
        int mapped_idx = mesh_manager->element_index(element_id);
        REQUIRE(mapped_idx == static_cast<int>(idx));
      }

      size_t num_vertices = mesh_manager->num_vertices();
      REQUIRE(num_vertices == TestType::num_vertices);
      for (size_t idx = 0; idx < num_vertices; ++idx) {
        MeshID vertex_id = mesh_manager->vertex_id(idx);
        // MOAB vertex IDs start at 1 and, for this model, are contiguous
        REQUIRE(vertex_id == idx + 1);
        int mapped_idx = mesh_manager->vertex_index(vertex_id);
        REQUIRE(mapped_idx == static_cast<int>(idx));
      }
    }

    // test mapping for non-contiguous MOAB IDs via modification
    {
      std::shared_ptr<MOABMeshManager> mesh_manager = std::make_shared<MOABMeshManager>();
      mesh_manager->load_file(filename);

      moab::Interface* mbi = mesh_manager->moab_interface();

      // create gaps in the intrinsic ID space by deleteing some elements
      moab::Range element_range;
      mbi->get_entities_by_type(0, moab::MBTET, element_range);
      moab::Range hex_range;
      mbi->get_entities_by_type(0, moab::MBHEX, hex_range);
      element_range.merge(hex_range);

      int next_id = 0;
      std::vector<MeshID> modified_ids;
      for (const auto& elem : element_range) {
        if (next_id % 100 == 0) {
          mbi->delete_entities(&elem, 1);
          next_id++;
          continue;
        }
        next_id ++;
        modified_ids.push_back(mbi->id_from_handle(elem));
      }

      mesh_manager->init();

      size_t num_elements = mesh_manager->num_volume_elements();
      REQUIRE(num_elements == modified_ids.size());
      for (size_t idx = 0; idx < num_elements; ++idx) {
        MeshID expected_id = modified_ids[idx];
        MeshID element_id = mesh_manager->element_id(idx);
        REQUIRE(element_id == expected_id);
        int mapped_idx = mesh_manager->element_index(element_id);
        REQUIRE(mapped_idx == static_cast<int>(idx));
      }

    }

    {
      std::shared_ptr<MOABMeshManager> mesh_manager = std::make_shared<MOABMeshManager>();
      mesh_manager->load_file(filename);

      moab::Interface* mbi = mesh_manager->moab_interface();

      moab::Range vertex_range;
      mbi->get_entities_by_type(0, moab::MBVERTEX, vertex_range);
      std::vector<MeshID> modified_vertex_ids;
      int next_id = 0;
      for (const auto& vertex : vertex_range) {
        if (next_id % 50 == 0) {
          // delete any elements adjacent to this vertex to avoid dangling references
          moab::Range adj_elems;
          mbi->get_adjacencies(&vertex, 1, 3, true, adj_elems);
          for (const auto& adj_elem : adj_elems) {
            mbi->delete_entities(&adj_elem, 1);
          }
          mbi->delete_entities(&vertex, 1);
          next_id++;
          continue;
        }
        modified_vertex_ids.push_back(mbi->id_from_handle(vertex));
        next_id++;
      }

      mesh_manager->init();

      size_t num_vertices = mesh_manager->num_vertices();
      REQUIRE(num_vertices == modified_vertex_ids.size());
      for (size_t idx = 0; idx < num_vertices; ++idx) {
        MeshID expected_id = modified_vertex_ids[idx];
        MeshID vertex_id = mesh_manager->vertex_id(idx);
        REQUIRE(vertex_id == expected_id);
        int mapped_idx = mesh_manager->vertex_index(vertex_id);
        REQUIRE(mapped_idx == static_cast<int>(idx));
      }
    }
  }
}


TEMPLATE_TEST_CASE("Test MOAB Transport", "[moab][transport]",
                   JezebelH5M,
                   JezebelQuadsH5M,
                   CylBrickH5M,
                   CylBrickQuadsH5M)
{
  std::string filename {TestType::filename};

  DYNAMIC_SECTION("Model: " << filename) {
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(filename);
    mesh_manager->init();
    mesh_manager->parse_metadata();
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


TEST_CASE("Test Hex Point Location Jezebel Quads")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel-quads.h5m");
  mesh_manager->init();
  xdg->prepare_raytracer();

  Position inside {0.0, 0.0, 0.0};
  REQUIRE(xdg->find_element(inside) != ID_NONE);

  Position outside {0.0, 0.0, 100.0};
  REQUIRE(xdg->find_element(outside) == ID_NONE);
}

TEST_CASE("Test Hex Element Walk Jezebel Quads")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel-quads.h5m");
  mesh_manager->init();
  xdg->prepare_raytracer();

  Position inside {0.0, 0.0, 0.0};
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
