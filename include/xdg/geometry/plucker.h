#ifndef _XDG_PLUCKER_H
#define _XDG_PLUCKER_H

#include "xdg/vec3da.h"
#include "xdg/geometry/dp_math.h"

namespace xdg {


/*
 * Triangle vertex ordering convention:
 *
 *      v2
 *     /  \
 *    /    \
 *   /      \
 *  /        \
 * v0--------v1
 *
 * The vertices are ordered counter-clockwise when viewed from the front face
 * (normal pointing out of the plane). This ordering is based on the reference:
 * https://doi.org/10.1002/cnm.1237
 */

struct PluckerIntersectionResult {
  bool hit = false;    // Whether an intersection occurred
  double t = 0.0;      // Distance along the ray to the intersection point
};

PluckerIntersectionResult plucker_ray_tri_intersect(const std::array<dp::vec3, 3> vertices,
                               const dp::vec3& origin,
                               const dp::vec3& direction,
                               const double nonneg_ray_len = INFTY,
                               const double* neg_ray_len = nullptr,
                               const int* orientation = nullptr);

} // namespace xdg

#endif // include guard