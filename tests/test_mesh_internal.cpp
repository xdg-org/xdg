#include <memory>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mesh_mock.h"

#include "xdg/ray_tracing_interface.h"

using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  REQUIRE(mm->num_volume_elements() == 12);
  REQUIRE(mm->num_volume_elements(1) == 12);
}

TEST_CASE("Mesh Mock Get Surface Mesh")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MeshMock>();
  mesh_manager->init(); // This should do nothing for the mock
  float fpTol = 1e-5;

  // Expected connectivity as local indices for each surface
  std::vector<std::vector<int>> expected_connectivity =
  {
    {0, 1, 2, 0, 2, 3}, // Surface 0
    {0, 1, 2, 0, 2, 3}, // Surface 1
    {0, 1, 2, 0, 2, 3}, // Surface 2
    {0, 1, 2, 0, 2, 3}, // Surface 3
    {0, 1, 2, 0, 2, 3}, // Surface 4
    {0, 1, 2, 0, 3, 1}, // Surface 5
  };

  std::vector<std::vector<Vertex>> expected_vertices = {
    // Surface 0
    { {5.0, -3.0, 7.0}, {5.0,  6.0, 7.0}, {-2.0, 6.0, 7.0}, {-2.0, -3.0, 7.0} },
    // Surface 1
    { {5.0, -3.0, -4.0}, {-2.0, 6.0, -4.0}, {5.0,  6.0, -4.0}, {-2.0, -3.0, -4.0} },
    // Surface 2
    { {5.0, -3.0,  7.0}, {5.0, 6.0, -4.0}, {5.0,  6.0,  7.0}, {5.0, -3.0, -4.0} },
    // Surface 3
    { {-2.0, 6.0, 7.0}, {-2.0, 6.0, -4.0}, {-2.0, -3.0, -4.0}, {-2.0, -3.0, 7.0} },
    // Surface 4
    { {5.0,  6.0,  7.0}, {5.0, 6.0, -4.0}, {-2.0,  6.0,  -4.0}, {-2.0, 6.0, 7.0} },
    // Surface 5
    { {5.0, -3.0,  7.0}, {-2.0, -3.0,  -4.0}, {5.0, -3.0, -4.0}, {-2.0, -3.0, 7.0} }
  };

  size_t surface_index = 0;
  // Loop over every surface in the MeshMock and test the connectivity and vertices
  for (const auto surface : mesh_manager->surfaces()) {
    auto surfaceMesh = mesh_manager->get_surface_mesh(surface);
    auto vertices = surfaceMesh.first;
    auto connectivity = surfaceMesh.second;

    // Test connectivity
    // two triangles per surface in the mocked mesh
    REQUIRE(connectivity.size() == 6);
    for (size_t i = 0; i < connectivity.size(); ++i) {
      REQUIRE(connectivity[i] == expected_connectivity[surface_index][i]);
      break;
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