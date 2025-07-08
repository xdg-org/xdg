// stl includes
#include <memory>
#include <numeric>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/xdg.h"
#include "xdg/embree/ray_tracer.h"

using namespace xdg;

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

TEST_CASE("Test BVH Build")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MOABMeshManager>();

  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);

  std::unique_ptr<RayTracer> ray_tracing_interface = std::make_unique<EmbreeRayTracer>();

  for (auto volume : mesh_manager->volumes()) {
    ray_tracing_interface->register_volume(mesh_manager, volume);
  }

  REQUIRE(ray_tracing_interface->num_registered_trees() == 2);
}

TEST_CASE("Test Ray Fire MOAB")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();
  xdg->prepare_raytracer();


  MeshID volume = mesh_manager->volumes()[0];

  Position origin {0.0, 0.0, 0.0};
  Direction direction {1.0, 0.0, 0.0};
  std::pair<double, MeshID> intersection;

  intersection = xdg->ray_fire(volume, origin, direction);

  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  origin = {3.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

  origin = {-10.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(15.0, 1e-6));

  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));

}

TEST_CASE("MOAB Element Types")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  for (const auto surface: mesh_manager->surfaces()) {
    auto element_type = mesh_manager->get_surface_element_type(surface);
    REQUIRE(element_type == SurfaceElementType::TRI); // Each surface should be made up of triangles
  }
}

TEST_CASE("MOAB Get Surface Mesh")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("overlap-edge.h5m");
  mesh_manager->init();

  float fpTol = 1e-5;

  // Define the expected connectivity and vertices for each surface
  std::vector<std::vector<int>> expected_connectivity = {
      {2, 3, 5, 3, 0, 4, 5, 4, 1, 3, 4, 5},                   /* Surface 1 */
      {5, 4, 1, 4, 7, 3, 2, 3, 6, 4, 5, 7, 5, 0, 7, 7, 6, 3}, /* Surface 2 */
      {5, 0, 7, 5, 4, 2, 1, 3, 6, 3, 4, 7, 4, 5, 7, 3, 7, 6}, /* Surface 3 */
      {5, 4, 0, 3, 4, 6, 1, 3, 6, 4, 5, 7, 5, 2, 7, 4, 7, 6}, /* Surface 4 */
      {3, 0, 4, 5, 1, 3, 5, 3, 4, 5, 4, 2},                   /* Surface 5 */
      {4, 5, 6, 0, 5, 3, 7, 2, 4, 4, 3, 5, 7, 4, 6, 7, 6, 1}, /* Surface 6 */
      {6, 4, 5, 3, 5, 4, 1, 5, 3, 7, 0, 4, 7, 4, 6, 7, 6, 2}, /* Surface 7 */
      {3, 6, 4, 1, 5, 3, 7, 2, 4, 3, 5, 6, 7, 4, 6, 7, 6, 0}  /* Surface 8 */
  };

  std::vector<std::vector<Vertex>> expected_vertices = {
      // Surface 1
      {
          {0.5, -0.866025, -1.5}, {-1, 0.0, -1.5}, {0.5, 0.866025, -1.5},
          {0.5, 0, -1.5}, {-0.25, -0.433013, -1.5}, {-0.25, 0.433013, -1.5}
      },
      // Surface 2
      {
          {0.5, -0.866025, -1.5}, {-1, 0.0, -1.5}, {0, 0, 1.5},
          {-0.333333, 0.0, 0.5}, {-0.666667, 0.0, -0.5},
          {-0.25, -0.433013, -1.5}, {0.166667, -0.288675, 0.5}, {0.333333, -0.57735, -0.5}
      },
      // Surface 3
      {
          {-1, 0.0, -1.5}, {0, 0, 1.5}, {0.5, 0.866025, -1.5},
          {0.166667, 0.288675, 0.5}, {0.333333, 0.57735, -0.5}, {-0.25, 0.433013, -1.5},
          {-0.333333, 0.0, 0.5}, {-0.666667, 0.0, -0.5}
      },
      // Surface 4
      {
          {0.5, -0.866025, -1.5}, {0, 0, 1.5}, {0.5, 0.866025, -1.5},
          {0.166667, -0.288675, 0.5}, {0.333333, -0.57735, -0.5}, {0.5, 0, -1.5},
          {0.166667, 0.288675, 0.5}, {0.333333, 0.57735, -0.5}
      },
      // Surface 5
      {
          {0.5, 1.71603, 1.5}, {0.5, -0.0160254, 1.5}, {-1, 0.85, 1.5},
          {0.5, 0.85, 1.5}, {-0.25, 1.28301, 1.5}, {-0.25, 0.416987, 1.5}
      },
      // Surface 6
      {
          {0, 0.85, -1.5}, {0.5, -0.0160254, 1.5}, {-1, 0.85, 1.5},
          {-0.333333, 0.85, -0.5}, {-0.666667, 0.85, 0.5}, {0.166667, 0.561325, -0.5},
          {0.333333, 0.27265, 0.5}, {-0.25, 0.416987, 1.5}
      },
      // Surface 7
      {
          {0.5, 1.71603, 1.5}, {0, 0.85, -1.5}, {-1, 0.85, 1.5},
          {0.166667, 1.13868, -0.5}, {0.333333, 1.42735, 0.5}, {-0.333333, 0.85, -0.5},
          {-0.666667, 0.85, 0.5}, {-0.25, 1.28301, 1.5}
      },
      // Surface 8
      {
          {0.5, 1.71603, 1.5}, {0, 0.85, -1.5}, {0.5, -0.0160254, 1.5},
          {0.166667, 0.561325, -0.5}, {0.333333, 0.27265, 0.5}, {0.166667, 1.13868, -0.5},
          {0.333333, 1.42735, 0.5}, {0.5, 0.85, 1.5}
      }
  };

  size_t surface_index = 0;
  for (const auto surface : mesh_manager->surfaces()) {
    auto surfaceMesh = mesh_manager->get_surface_mesh(surface);
    auto vertices = surfaceMesh.first;
    auto connectivity = surfaceMesh.second;

    // Test connectivity
    REQUIRE(connectivity.size() == expected_connectivity[surface_index].size());
    for (size_t i = 0; i < connectivity.size(); ++i) {
      REQUIRE(connectivity[i] == expected_connectivity[surface_index][i]);
    }

    // Test vertices
    REQUIRE(vertices.size() == expected_vertices[surface_index].size());
    for (size_t i = 0; i < vertices.size(); ++i) {
      REQUIRE_THAT(vertices[i].x, Catch::Matchers::WithinAbs(expected_vertices[surface_index][i].x, fpTol));
      REQUIRE_THAT(vertices[i].y, Catch::Matchers::WithinAbs(expected_vertices[surface_index][i].y, fpTol));
      REQUIRE_THAT(vertices[i].z, Catch::Matchers::WithinAbs(expected_vertices[surface_index][i].z, fpTol));
    }

    ++surface_index;
  }
}

TEST_CASE("TEST MOAB Find Element Method")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.h5m");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

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