#ifndef XDG_GEOMETRY_TRIANGLE_INTERSECT_H
#define XDG_GEOMETRY_TRIANGLE_INTERSECT_H

#include <array>
#include "xdg/vec3da.h"

namespace xdg {

inline Direction triangle_normal(const std::array<Vertex, 3>& vertices) {
  const Position v1 = vertices[1] - vertices[0];
  const Position v2 = vertices[2] - vertices[0];
  return (v1.cross(v2)).normalize();
}

} // namespace xdg

#endif // XDG_GEOMETRY_TRIANGLE_INTERSECT_H