// stl includes
#include <array>
#include <memory>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Face Connectivity (MeshMock)", "[surface][unit]") {
  auto mock_mesh = std::make_shared<MeshMock>();
  std::shared_ptr<MeshManager> mesh_manager = mock_mesh;
  mesh_manager->init();

  // Get connectivity directly from MeshMock for comparison
  const auto& expected_face_connectivity = mock_mesh->triangle_connectivity();

  REQUIRE(mesh_manager->num_surfaces() == 6);
  size_t total_faces = 0;

  for (const auto surface : mesh_manager->surfaces()) {
    const auto faces = mesh_manager->get_surface_faces(surface);
    REQUIRE(faces.size() == 2);

    for (const auto face : faces) {
      const auto conn = mesh_manager->face_connectivity(face);
      const auto expected = expected_face_connectivity.at(face);
      REQUIRE(conn.size() == expected.size());

      auto conn_it = conn.begin();
      for (const auto expected_id : expected) {
        REQUIRE(conn_it != conn.end());
        REQUIRE(*conn_it == expected_id);
        ++conn_it;
      }
      ++total_faces;
    }
  }

  REQUIRE(total_faces == expected_face_connectivity.size());
}

TEST_CASE("Face Vertices (MeshMock)", "[surface][unit]") {
  auto mock_mesh = std::make_shared<MeshMock>();
  std::shared_ptr<MeshManager> mesh_manager = mock_mesh;
  mesh_manager->init();

  // Get vertices and connectivity directly from the MeshMock for comparison
  const auto& expected_face_connectivity = mock_mesh->triangle_connectivity();
  const auto& expected_vertices = mock_mesh->vertices();

  MeshID face = 0;
  for (const auto& expected_conn : expected_face_connectivity) {
    const auto verts = mesh_manager->face_vertices(face++);

    auto vert_it = verts.begin();
    for (const auto vertex_id : expected_conn) {
      REQUIRE(vert_it != verts.end());
      const auto& expected_vertex = expected_vertices.at(vertex_id);
      REQUIRE_THAT((*vert_it)[0], Catch::Matchers::WithinAbs(expected_vertex[0], 1e-12));
      REQUIRE_THAT((*vert_it)[1], Catch::Matchers::WithinAbs(expected_vertex[1], 1e-12));
      REQUIRE_THAT((*vert_it)[2], Catch::Matchers::WithinAbs(expected_vertex[2], 1e-12));
      ++vert_it;
    }
  }
}

TEST_CASE("Boundary Face Element (MeshMock)", "[surface][unit]") {
  auto mock_mesh = std::make_shared<MeshMock>();
  std::shared_ptr<MeshManager> mesh_manager = mock_mesh;
  mesh_manager->init();

  for (MeshID face = 0; face < static_cast<MeshID>(mock_mesh->triangle_connectivity().size()); ++face) {
    const auto element = mesh_manager->get_boundary_face_element(face);
    REQUIRE(element != ID_NONE);

    const auto& tri = mock_mesh->triangle_connectivity().at(face);
    const auto& tet = mock_mesh->tetrahedron_connectivity().at(element);
    const auto tet_faces = mock_mesh->tet_faces(tet);

    bool found_match = false;
    for (auto tet_face : tet_faces) {
      std::sort(tet_face.begin(), tet_face.end());
      auto sorted_tri = tri;
      std::sort(sorted_tri.begin(), sorted_tri.end());
      if (tet_face == sorted_tri) {
        found_match = true;
        break;
      }
    }
    REQUIRE(found_match);
  }
}

TEST_CASE("Get Surface Connectivity (MeshMock)", "[surface][unit]") {
  auto mock_mesh = std::make_shared<MeshMock>();
  std::shared_ptr<MeshManager> mesh_manager = mock_mesh;
  mesh_manager->init();

  const std::vector<std::vector<int>> expected_connectivity = {
    {0, 1, 2, 0, 2, 3}, // surface 0
    {0, 2, 1, 0, 3, 2}, // surface 1
    {0, 3, 1, 0, 2, 3}, // surface 2
    {0, 2, 3, 0, 3, 1}, // surface 3
    {0, 2, 3, 0, 3, 1}, // surface 4
    {0, 3, 2, 0, 1, 3}  // surface 5
  };

  REQUIRE(mesh_manager->num_surfaces() == static_cast<int>(expected_connectivity.size()));
  auto expected_it = expected_connectivity.begin();
  for (const auto surface : mesh_manager->surfaces()) {
    REQUIRE(expected_it != expected_connectivity.end());
    const auto connectivity = mesh_manager->get_surface_connectivity(surface);
    REQUIRE(connectivity == *expected_it);
    ++expected_it;
  }
  REQUIRE(expected_it == expected_connectivity.end());
}

TEST_CASE("Get Volume Connectivity (MeshMock)", "[volume][unit]") {
  auto mock_mesh = std::make_shared<MeshMock>();
  std::shared_ptr<MeshManager> mesh_manager = mock_mesh;
  mesh_manager->init();

  const std::vector<int> expected_connectivity = {
    0, 1, 2, 8,
    0, 2, 3, 8,
    4, 6, 5, 8,
    4, 7, 6, 8,
    0, 5, 1, 8,
    0, 4, 5, 8,
    2, 6, 7, 8,
    2, 7, 3, 8,
    0, 7, 4, 8,
    0, 3, 7, 8,
    1, 5, 6, 8,
    1, 6, 2, 8
  };

  REQUIRE(mesh_manager->num_volumes() == 1);
  const auto connectivity = mesh_manager->get_volume_connectivity(mesh_manager->volumes()[0]);
  REQUIRE(connectivity == expected_connectivity);
}
