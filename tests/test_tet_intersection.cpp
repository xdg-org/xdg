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
  REQUIRE(plucker_tet_containment_test(inside_point, v0, v1, v2, v3) == true);
  REQUIRE(plucker_tet_containment_test(outside_point, v0, v1, v2, v3) == false);
  REQUIRE(plucker_tet_containment_test(boundary_point, v0, v1, v2, v3) == true);

  // Check point that is co-planar with one of the faces, but outside the tet
  Position coplanar_exterior_point(-0.5, -0.5, 0.0);
  REQUIRE(plucker_tet_containment_test(coplanar_exterior_point, v0, v1, v2, v3) == false);
}