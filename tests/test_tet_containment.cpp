// stl includes
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/ray_tracing_interface.h"
#include "xdg/tetrahedron_contain.h"
#include "xdg/vec3da.h"

using namespace xdg;

TEST_CASE("Tetrahedron Intersection Unit Test")
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
  CHECK(plucker_tet_containment_test(inside_point, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(outside_point, v0, v1, v2, v3) == false);
  CHECK(plucker_tet_containment_test(boundary_point, v0, v1, v2, v3) == true);

  // Check point that is co-planar with one of the faces, but outside the tet
  Position coplanar_exterior_point(-0.5, -0.5, 0.0);
  CHECK(plucker_tet_containment_test(coplanar_exterior_point, v0, v1, v2, v3) == false);

  // Test points near the faces but not coplanar
  Position near_face012(0.3, 0.3, 0.1);
  Position near_face013(0.3, 0.1, 0.3);
  Position near_face023(0.1, 0.3, 0.3);
  Position near_face123(0.3, 0.3, 0.3);

  CHECK(plucker_tet_containment_test(near_face012, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(near_face013, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(near_face023, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(near_face123, v0, v1, v2, v3) == true);

  // Test points just outside the tetrahedron
  Position just_outside_face012(0.3, 0.3, -0.1);
  Position just_outside_face013(0.3, -0.1, 0.3);
  Position just_outside_face023(-0.1, 0.3, 0.3);
  Position just_outside_face123(0.4, 0.4, 0.4);

  CHECK(plucker_tet_containment_test(just_outside_face012, v0, v1, v2, v3) == false);
  CHECK(plucker_tet_containment_test(just_outside_face013, v0, v1, v2, v3) == false);
  CHECK(plucker_tet_containment_test(just_outside_face023, v0, v1, v2, v3) == false);
  CHECK(plucker_tet_containment_test(just_outside_face123, v0, v1, v2, v3) == false);

  // Test points just inside the tetrahedron
  Position just_inside_face012(0.3, 0.3, 0.1);
  Position just_inside_face013(0.3, 0.1, 0.3);
  Position just_inside_face023(0.1, 0.3, 0.3);
  Position just_inside_face123(0.3, 0.3, 0.3);

  CHECK(plucker_tet_containment_test(just_inside_face012, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(just_inside_face013, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(just_inside_face023, v0, v1, v2, v3) == true);
  CHECK(plucker_tet_containment_test(just_inside_face123, v0, v1, v2, v3) == true);

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
  CHECK(plucker_tet_containment_test(inside_point_2, v0_2, v1_2, v2_2, v3_2) == true);
  CHECK(plucker_tet_containment_test(outside_point_2, v0_2, v1_2, v2_2, v3_2) == false);
  CHECK(plucker_tet_containment_test(boundary_point_2, v0_2, v1_2, v2_2, v3_2) == true);

  // Test points near faces of second tetrahedron
  Position near_face_2(1.3, 1.3, 1.1);
  Position just_outside_face_2(1.3, 1.3, 0.9);

  CHECK(plucker_tet_containment_test(near_face_2, v0_2, v1_2, v2_2, v3_2) == true);
  CHECK(plucker_tet_containment_test(just_outside_face_2, v0_2, v1_2, v2_2, v3_2) == false);

  // Test points that are in one tet but not the other
  CHECK(plucker_tet_containment_test(inside_point, v0_2, v1_2, v2_2, v3_2) == false);
  CHECK(plucker_tet_containment_test(inside_point_2, v0, v1, v2, v3) == false);
}