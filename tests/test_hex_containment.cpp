// stl includes
#include <array>

// testing includes
#include <catch2/catch_test_macros.hpp>

#include "xdg/hexahedron_contain.h"
#include "xdg/vec3da.h"

using namespace xdg;

TEST_CASE("Hexahedron Containment Unit Test")
{
  // Define hexahedron vertices in MBCN Hex8 ordering.
  const std::array<Vertex, 8> verts = {
    Vertex(0.0, 0.0, 0.0),
    Vertex(1.0, 0.0, 0.0),
    Vertex(1.0, 1.0, 0.0),
    Vertex(0.0, 1.0, 0.0),
    Vertex(0.0, 0.0, 1.0),
    Vertex(1.0, 0.0, 1.0),
    Vertex(1.0, 1.0, 1.0),
    Vertex(0.0, 1.0, 1.0),
  };

  // Test points
  Position inside_point(0.5, 0.5, 0.5); // Inside the hexahedron
  Position outside_point(2.0, 2.0, 2.0); // Clearly outside
  Position boundary_point(0.0, 0.5, 0.5); // On a face

  // Check containment
  CHECK(hex_containment_test(inside_point, verts) == true);
  CHECK(hex_containment_test(outside_point, verts) == false);
  CHECK(hex_containment_test(boundary_point, verts) == true);

  // Check point that is co-planar with one of the faces, but outside the hex.
  Position coplanar_exterior_point(1.5, 0.5, 0.0);
  CHECK(hex_containment_test(coplanar_exterior_point, verts) == false);

  // Test points near the faces but not coplanar.
  Position near_bottom_face(0.5, 0.5, 0.1);
  Position near_top_face(0.5, 0.5, 0.9);
  Position near_front_face(0.5, 0.1, 0.5);
  Position near_right_face(0.9, 0.5, 0.5);
  Position near_back_face(0.5, 0.9, 0.5);
  Position near_left_face(0.1, 0.5, 0.5);

  CHECK(hex_containment_test(near_bottom_face, verts) == true);
  CHECK(hex_containment_test(near_top_face, verts) == true);
  CHECK(hex_containment_test(near_front_face, verts) == true);
  CHECK(hex_containment_test(near_right_face, verts) == true);
  CHECK(hex_containment_test(near_back_face, verts) == true);
  CHECK(hex_containment_test(near_left_face, verts) == true);

  // Test points just outside the hexahedron.
  Position just_outside_bottom_face(0.5, 0.5, -0.1);
  Position just_outside_top_face(0.5, 0.5, 1.1);
  Position just_outside_front_face(0.5, -0.1, 0.5);
  Position just_outside_right_face(1.1, 0.5, 0.5);
  Position just_outside_back_face(0.5, 1.1, 0.5);
  Position just_outside_left_face(-0.1, 0.5, 0.5);

  CHECK(hex_containment_test(just_outside_bottom_face, verts) == false);
  CHECK(hex_containment_test(just_outside_top_face, verts) == false);
  CHECK(hex_containment_test(just_outside_front_face, verts) == false);
  CHECK(hex_containment_test(just_outside_right_face, verts) == false);
  CHECK(hex_containment_test(just_outside_back_face, verts) == false);
  CHECK(hex_containment_test(just_outside_left_face, verts) == false);

  // Test points just inside the hexahedron.
  Position just_inside_bottom_face(0.5, 0.5, 0.1);
  Position just_inside_top_face(0.5, 0.5, 0.9);
  Position just_inside_front_face(0.5, 0.1, 0.5);
  Position just_inside_right_face(0.9, 0.5, 0.5);
  Position just_inside_back_face(0.5, 0.9, 0.5);
  Position just_inside_left_face(0.1, 0.5, 0.5);

  CHECK(hex_containment_test(just_inside_bottom_face, verts) == true);
  CHECK(hex_containment_test(just_inside_top_face, verts) == true);
  CHECK(hex_containment_test(just_inside_front_face, verts) == true);
  CHECK(hex_containment_test(just_inside_right_face, verts) == true);
  CHECK(hex_containment_test(just_inside_back_face, verts) == true);
  CHECK(hex_containment_test(just_inside_left_face, verts) == true);

  // Check lower-dimensional boundaries.
  Position edge_point(1.0, 0.0, 0.5);
  Position vertex_point(1.0, 1.0, 1.0);
  CHECK(hex_containment_test(edge_point, verts) == true);
  CHECK(hex_containment_test(vertex_point, verts) == true);

  // Create a second hexahedron with different position and dimensions.
  const std::array<Vertex, 8> verts_2 = {
    Vertex(1.0, 1.0, 1.0),
    Vertex(3.0, 1.0, 1.0),
    Vertex(3.0, 4.0, 1.0),
    Vertex(1.0, 4.0, 1.0),
    Vertex(1.0, 1.0, 5.0),
    Vertex(3.0, 1.0, 5.0),
    Vertex(3.0, 4.0, 5.0),
    Vertex(1.0, 4.0, 5.0),
  };

  // Test points for second hexahedron.
  Position inside_point_2(2.0, 2.0, 3.0);
  Position outside_point_2(0.0, 0.0, 0.0);
  Position boundary_point_2(1.0, 2.0, 3.0);

  // Check containment for second hexahedron.
  CHECK(hex_containment_test(inside_point_2, verts_2) == true);
  CHECK(hex_containment_test(outside_point_2, verts_2) == false);
  CHECK(hex_containment_test(boundary_point_2, verts_2) == true);

  // Test points near faces of second hexahedron.
  Position near_face_2(2.0, 2.0, 1.1);
  Position just_outside_face_2(2.0, 2.0, 0.9);

  CHECK(hex_containment_test(near_face_2, verts_2) == true);
  CHECK(hex_containment_test(just_outside_face_2, verts_2) == false);

  // Test points that are in one hex but not the other.
  CHECK(hex_containment_test(inside_point, verts_2) == false);
  CHECK(hex_containment_test(inside_point_2, verts) == false);
}
