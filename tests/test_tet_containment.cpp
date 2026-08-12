// stl includes
#include <array>
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/ray_tracing_interface.h"
#include "xdg/tetrahedron_contain.h"
#include "xdg/vec3da.h"

#include "mesh_mocks.h"

using namespace xdg;

TEST_CASE("MockedTriTetMesh Face Representation")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MockedTriTetMesh>();
  mm->init();

  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(0) == 12);
  REQUIRE(mm->num_volume_elements(0) == 12);
  REQUIRE(mm->get_volume_elements(0) ==
          std::vector<MeshID> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
  REQUIRE(mm->element_connectivity(0) == std::vector<MeshID> {0, 1, 2, 8});
  REQUIRE(mm->element_connectivity(1) == std::vector<MeshID> {0, 2, 3, 8});
  REQUIRE(mm->adjacent_element(0, 1) == 1);
  REQUIRE(mm->adjacent_element(1, 2) == 0);

  const std::array<Direction, 6> expected_normals {
    Direction {0.0, 0.0, 1.0},
    Direction {0.0, 0.0, -1.0},
    Direction {1.0, 0.0, 0.0},
    Direction {-1.0, 0.0, 0.0},
    Direction {0.0, 1.0, 0.0},
    Direction {0.0, -1.0, 0.0},
  };

  for (MeshID surface = 0; surface < 6; ++surface) {
    REQUIRE(mm->get_surface_face_type(surface) == SurfaceFaceType::TRI);
    const auto faces = mm->get_surface_faces(surface);
    REQUIRE(mm->get_surface_connectivity(surface).size() == faces.size() * 3);
    for (const auto face : faces) {
      REQUIRE(mm->face_vertices(face).size() == 3);

      const auto normal = mm->face_normal(face);
      REQUIRE_THAT(normal.x, Catch::Matchers::WithinAbs(expected_normals[surface].x, 1e-12));
      REQUIRE_THAT(normal.y, Catch::Matchers::WithinAbs(expected_normals[surface].y, 1e-12));
      REQUIRE_THAT(normal.z, Catch::Matchers::WithinAbs(expected_normals[surface].z, 1e-12));
    }
  }

  const auto bbox = mm->volume_bounding_box(0);
  REQUIRE_THAT(bbox.min_x, Catch::Matchers::WithinAbs(-2.0, 1e-12));
  REQUIRE_THAT(bbox.min_y, Catch::Matchers::WithinAbs(-3.0, 1e-12));
  REQUIRE_THAT(bbox.min_z, Catch::Matchers::WithinAbs(-4.0, 1e-12));
  REQUIRE_THAT(bbox.max_x, Catch::Matchers::WithinAbs(5.0, 1e-12));
  REQUIRE_THAT(bbox.max_y, Catch::Matchers::WithinAbs(6.0, 1e-12));
  REQUIRE_THAT(bbox.max_z, Catch::Matchers::WithinAbs(7.0, 1e-12));
}

TEST_CASE("Tetrahedron Containment Unit Test")
{
  // Define tetrahedron vertices
  Position v0(0.0, 0.0, 0.0);
  Position v1(1.0, 0.0, 0.0);
  Position v2(0.0, 1.0, 0.0);
  Position v3(0.0, 0.0, 1.0);

  // Test points
  Position inside_point(0.1, 0.1, 0.1); // Inside the tetrahedron
  Position outside_point(2.0, 2.0, 2.0); // Clearly outside
  Position boundary_point(0.0, 0.0, 0.5); // On a face

  // Check containment
  CHECK(tet_containment_test(inside_point, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(outside_point, v0, v1, v2, v3) == false);
  CHECK(tet_containment_test(boundary_point, v0, v1, v2, v3) == true);

  // Check point that is co-planar with one of the faces, but outside the tet
  Position coplanar_exterior_point(-0.5, -0.5, 0.0);
  CHECK(tet_containment_test(coplanar_exterior_point, v0, v1, v2, v3) == false);

  // Test points near the faces but not coplanar
  Position near_face012(0.3, 0.3, 0.1);
  Position near_face013(0.3, 0.1, 0.3);
  Position near_face023(0.1, 0.3, 0.3);
  Position near_face123(0.3, 0.3, 0.3);

  CHECK(tet_containment_test(near_face012, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(near_face013, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(near_face023, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(near_face123, v0, v1, v2, v3) == true);

  // Test points just outside the tetrahedron
  Position just_outside_face012(0.3, 0.3, -0.1);
  Position just_outside_face013(0.3, -0.1, 0.3);
  Position just_outside_face023(-0.1, 0.3, 0.3);
  Position just_outside_face123(0.4, 0.4, 0.4);

  CHECK(tet_containment_test(just_outside_face012, v0, v1, v2, v3) == false);
  CHECK(tet_containment_test(just_outside_face013, v0, v1, v2, v3) == false);
  CHECK(tet_containment_test(just_outside_face023, v0, v1, v2, v3) == false);
  CHECK(tet_containment_test(just_outside_face123, v0, v1, v2, v3) == false);

  // Test points just inside the tetrahedron
  Position just_inside_face012(0.3, 0.3, 0.1);
  Position just_inside_face013(0.3, 0.1, 0.3);
  Position just_inside_face023(0.1, 0.3, 0.3);
  Position just_inside_face123(0.3, 0.3, 0.3);

  CHECK(tet_containment_test(just_inside_face012, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(just_inside_face013, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(just_inside_face023, v0, v1, v2, v3) == true);
  CHECK(tet_containment_test(just_inside_face123, v0, v1, v2, v3) == true);

  // Create a second tetrahedron with different orientation
  Position v0_2(1.0, 1.0, 1.0);
  Position v1_2(2.0, 1.0, 1.0);
  Position v2_2(1.0, 2.0, 1.0);
  Position v3_2(1.0, 1.0, 2.0);

  // Test points for second tetrahedron
  Position inside_point_2(1.2, 1.2, 1.2);
  Position outside_point_2(0.0, 0.0, 0.0);
  Position boundary_point_2(1.0, 1.0, 1.5);

  // Check containment for second tetrahedron
  CHECK(tet_containment_test(inside_point_2, v0_2, v1_2, v2_2, v3_2) == true);
  CHECK(tet_containment_test(outside_point_2, v0_2, v1_2, v2_2, v3_2) == false);
  CHECK(tet_containment_test(boundary_point_2, v0_2, v1_2, v2_2, v3_2) == true);

  // Test points near faces of second tetrahedron
  Position near_face_2(1.3, 1.3, 1.1);
  Position just_outside_face_2(1.3, 1.3, 0.9);

  CHECK(tet_containment_test(near_face_2, v0_2, v1_2, v2_2, v3_2) == true);
  CHECK(tet_containment_test(just_outside_face_2, v0_2, v1_2, v2_2, v3_2) == false);

  // Test points that are in one tet but not the other
  CHECK(tet_containment_test(inside_point, v0_2, v1_2, v2_2, v3_2) == false);
  CHECK(tet_containment_test(inside_point_2, v0, v1, v2, v3) == false);
}
