#include <array>
#include <memory>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mesh_mocks.h"

#include "xdg/ray_tracing_interface.h"

using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MockedTriTetMesh>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  REQUIRE(mm->num_volume_elements() == 12);
  REQUIRE(mm->num_volume_elements(1) == 12);
}

TEST_CASE("MockedQuadHexMesh Face Representation")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MockedQuadHexMesh>();
  mm->init();

  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(0) == 10);
  REQUIRE(mm->num_volume_elements(0) == 2);
  REQUIRE(mm->get_volume_elements(0) == std::vector<MeshID> {0, 1});
  REQUIRE(mm->element_connectivity(0) == std::vector<MeshID> {0, 1, 2, 3, 4, 5, 6, 7});
  REQUIRE(mm->element_connectivity(1) == std::vector<MeshID> {1, 8, 9, 2, 5, 10, 11, 6});
  REQUIRE(mm->adjacent_element(0, 3) == 1);
  REQUIRE(mm->adjacent_element(1, 5) == 0);

  const auto [right_element, right_distance] =
    mm->next_element(0, Position {0.5, 0.5, 0.5}, Direction {1.0, 0.0, 0.0});
  REQUIRE(right_element == 1);
  REQUIRE_THAT(right_distance, Catch::Matchers::WithinAbs(0.5, 1e-12));

  const auto [left_element, left_distance] =
    mm->next_element(1, Position {1.5, 0.5, 0.5}, Direction {-1.0, 0.0, 0.0});
  REQUIRE(left_element == 0);
  REQUIRE_THAT(left_distance, Catch::Matchers::WithinAbs(0.5, 1e-12));

  const std::array<Direction, 6> expected_normals {
    Direction {0.0, 0.0, 1.0},
    Direction {0.0, 0.0, -1.0},
    Direction {0.0, -1.0, 0.0},
    Direction {1.0, 0.0, 0.0},
    Direction {0.0, 1.0, 0.0},
    Direction {-1.0, 0.0, 0.0},
  };

  for (MeshID surface = 0; surface < 6; ++surface) {
    REQUIRE(mm->get_surface_face_type(surface) == SurfaceFaceType::QUAD);
    const auto faces = mm->get_surface_faces(surface);
    REQUIRE(mm->get_surface_connectivity(surface).size() == faces.size() * 4);
    for (const auto face : faces) {
      REQUIRE(mm->face_vertices(face).size() == 4);

      const auto normal = mm->face_normal(face);
      REQUIRE_THAT(normal.x, Catch::Matchers::WithinAbs(expected_normals[surface].x, 1e-12));
      REQUIRE_THAT(normal.y, Catch::Matchers::WithinAbs(expected_normals[surface].y, 1e-12));
      REQUIRE_THAT(normal.z, Catch::Matchers::WithinAbs(expected_normals[surface].z, 1e-12));
    }
  }

  auto bbox = mm->volume_bounding_box(0);
  REQUIRE_THAT(bbox.min_x, Catch::Matchers::WithinAbs(0.0, 1e-12));
  REQUIRE_THAT(bbox.min_y, Catch::Matchers::WithinAbs(0.0, 1e-12));
  REQUIRE_THAT(bbox.min_z, Catch::Matchers::WithinAbs(0.0, 1e-12));
  REQUIRE_THAT(bbox.max_x, Catch::Matchers::WithinAbs(2.0, 1e-12));
  REQUIRE_THAT(bbox.max_y, Catch::Matchers::WithinAbs(1.0, 1e-12));
  REQUIRE_THAT(bbox.max_z, Catch::Matchers::WithinAbs(1.0, 1e-12));
}
