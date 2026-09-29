#include "xdg/geometry/hexahedron_contain.h"

#include "xdg/constants.h"
#include "xdg/geometry/face_common.h"
#include "xdg/geometry/plucker.h"

namespace xdg
{

bool select_diagonal(const std::array<Vertex, 8>& verts,
                     const std::array<int, 4>& face)
{
  std::array<Vertex, 4> face_verts = {verts[face[0]], verts[face[1]], verts[face[2]], verts[face[3]]};
  return canonical_diagonal(face_verts);
}

bool hex_containment_test(const Position& point,
                          const std::array<Vertex, 8>& verts)
{
  static constexpr std::array<std::array<int, 4>, 6> k_hex_faces = {
    std::array<int, 4>{0, 3, 2, 1},
    std::array<int, 4>{4, 5, 6, 7},
    std::array<int, 4>{0, 1, 5, 4},
    std::array<int, 4>{1, 2, 6, 5},
    std::array<int, 4>{2, 3, 7, 6},
    std::array<int, 4>{3, 0, 4, 7}
  };

  Position centroid {0.0, 0.0, 0.0};
  for (const auto& v : verts) {
    centroid += v;
  }
  centroid = centroid / 8.0;

  Direction direction = centroid - point;
  const double distance_to_centroid = direction.length();
  if (distance_to_centroid <= TINY_BIT) {
    return true;
  }
  direction /= distance_to_centroid;

  auto crosses_boundary_triangle = [&](int i0, int i1, int i2) {
    std::array<Vertex, 3> triangle {verts[i0], verts[i1], verts[i2]};

    Direction normal = (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]);
    if (normal.dot(centroid - triangle[0]) > 0.0) {
      normal = -normal;
    }

    const double point_side = normal.dot(point - triangle[0]);
    const double centroid_side = normal.dot(centroid - triangle[0]);

    if (point_side <= dp::DBL_ZERO_TOL || centroid_side >= -dp::DBL_ZERO_TOL) {
      return false;
    }

    if (!plucker_line_intersects_triangle(triangle.data(), point, direction)) {
      return false;
    }

    return true;
  };

  for (const auto& face : k_hex_faces) {
    // diagonal is selected based on a lexicographic comparison
    // of vertex coordinates so diagonals are chosen consistently
    // regardless of face connectivity ordering
    if (select_diagonal(verts, face)) {
      if (crosses_boundary_triangle(face[0], face[1], face[2]) ||
          crosses_boundary_triangle(face[0], face[2], face[3])) {
        return false;
      }
    } else {
      if (crosses_boundary_triangle(face[1], face[2], face[3]) ||
          crosses_boundary_triangle(face[1], face[3], face[0])) {
        return false;
      }
    }
  }

  return true;
}

} // namespace xdg
