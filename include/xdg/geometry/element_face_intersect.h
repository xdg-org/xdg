#ifndef XDG_ELEMENT_FACE_INTERSECT_H
#define XDG_ELEMENT_FACE_INTERSECT_H

#include <memory>
#include <vector>

#include "xdg/geometry/face_common.h"
#include "xdg/geometry/measure.h"
#include "xdg/geometry/plucker.h"
#include "xdg/element_face_accessor.h"

namespace xdg {

//! A helper struct to store information about an intersection with an element face.
struct ElemFaceIntersectResult {
  bool hit {false};
  double distance {-INFTY};
  double exiting_dot {0.0};
};

//! \brief Determines the faces of a given element that are intersected by a ray.
//! \param r The ray origin
//! \param u The ray direction
//! \param efa The element face accessor to check a given face of
//! \param face The face to check in efa
//! \param flip_normal Whether the normal should be flipped or not. If false, 
//!                    this represents an exiting check. If true, this represents 
//!                    an entering check.
//! \return Whether or not the ray hit the face, alongside the hit distance and 
//!         exiting dot product (if a hit is registered).
inline ElemFaceIntersectResult intersect_elem_face(const Position& r, const Position& u, 
                                                   const ElementFaceAccessor& efa, int face, 
                                                   bool flip_normal = false) 
{
  const double nf = flip_normal ? -1.0 : 1.0;

  auto coords = efa.face_vertices(face);
  double exiting_dot;
  PluckerIntersectionResult result {false, INFTY};
  if (coords.size() == 3) {
    exiting_dot = nf * u.dot(triangle_normal(coords));
    if (exiting_dot < 0) return {false, -INFTY, 0.0};
    result = plucker_ray_tri_intersect(
        coords.data(), r, u, INFTY, -1e-10, false, 0);
  } else if (coords.size() == 4) {
    std::array<Vertex, 3> tri0;
    std::array<Vertex, 3> tri1;
    if (canonical_diagonal(coords)) {
      tri0 = {coords[0], coords[1], coords[2]};
      tri1 = {coords[0], coords[2], coords[3]};
    } else {
      tri0 = {coords[1], coords[2], coords[3]};
      tri1 = {coords[1], coords[3], coords[0]};
    }
    PluckerIntersectionResult result0, result1;
    double exiting_dot0 = nf * u.dot(triangle_normal(tri0));
    if (exiting_dot0 >= 0.0) {
      result0 = plucker_ray_tri_intersect(tri0.data(),
                                          r,
                                          u,
                                          INFTY,
                                          -1e-10,
                                          false,
                                          0);
    }

    double exiting_dot1 = nf * u.dot(triangle_normal(tri1));
    if (exiting_dot1 >= 0.0) {
      result1 = plucker_ray_tri_intersect(tri1.data(),
                                          r,
                                          u,
                                          INFTY,
                                          -1e-10,
                                          false,
                                          0);
    }
    if (!result0.hit && !result1.hit) return {false, -INFTY, 0.0};
    // we hit one or both triangles, so we will return a hit
    result.hit = true;
    // if we hit both triangles, use the closer triangle distance
    if (result0.hit) {
      result.t = result0.t;
      exiting_dot = exiting_dot0;
    }
    if (result1.hit && result1.t < result.t) {
      result.t = result1.t;
      exiting_dot = exiting_dot1;
    }
  } else {
    fatal_error("Unsupported face vertex count {} in next_element", coords.size());
  }

  if (!result.hit) return {false, -INFTY, 0.0};
  if (exiting_dot < 0.0) return {false, -INFTY, 0.0};

  return {true, result.t, exiting_dot};
}
} // namespace xdg

#endif // XDG_ELEMENT_FACE_INTERSECT_H