#ifndef _XDG_PLUCKER_H
#define _XDG_PLUCKER_H

#include "xdg/vec3da.h"

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
bool plucker_ray_tri_intersect(const std::array<Position, 3> vertices,
                               const Position& origin,
                               const Direction& direction,
                               double& dist_out,
                               const double nonneg_ray_len = INFTY,
                               const double* neg_ray_len = nullptr,
                               const int* orientation = nullptr);

} // namespace xdg

#endif // include guard