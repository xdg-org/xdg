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

constexpr PluckerIntersectionResult EXIT_EARLY = {false, 0.0};

/* Function to return the vertex with the lowest coordinates. To force the same
  ray-edge computation, the Plücker test needs to use consistent edge
  representation. This would be more simple with MOAB handles instead of
  coordinates... 
*/
inline bool first(const dp::vec3& a, const dp::vec3& b) {
  if (a[0] < b[0]) return true;  
  if (a[0] > b[0]) return false;

  if (a[1] < b[1]) return true;  
  if (a[1] > b[1]) return false;

  return a[2] < b[2];
}

inline double plucker_edge_test(const dp::vec3& vertexa, const dp::vec3& vertexb,
                                const dp::vec3& ray, const dp::vec3& ray_normal)
{
  double pip;
  if (lower(vertexa, vertexb)) {
    const dp::vec3 edge = vertexb - vertexa;
    const dp::vec3 edge_normal = dp::cross(edge, vertexa);
    pip = dp::dot(ray, edge_normal) + dp::dot(ray_normal, edge);
  } else {
    const dp::vec3 edge = vertexa - vertexb;
    const dp::vec3 edge_normal = dp::cross(edge, vertexb);
    pip = dp::dot(ray, edge_normal) + dp::dot(ray_normal, edge);
    pip = -pip;
  }
  if (PLUCKER_ZERO_TOL > dp::abs(pip))  // <-- absd
    pip = 0.0;
  return pip;
}

inline PluckerIntersectionResult plucker_ray_tri_intersect(const std::array<dp::vec3, 3> vertices,
                               const dp::vec3& origin,
                               const dp::vec3& direction,
                               const double nonneg_ray_len = INFTY,
                               const double* neg_ray_len = nullptr,
                               const int* orientation = nullptr)
{
  double dist_out = INFTY;

  const dp::vec3 raya = direction;
  const dp::vec3 rayb = direction.cross(origin);

  // Determine the value of the first Plucker coordinate from edge 0
  double plucker_coord0 =
    plucker_edge_test(vertices[0], vertices[1], raya, rayb);

  // If orientation is set, confirm that sign of plucker_coordinate indicate
  // correct orientation of intersection
  if (orientation && (*orientation) * plucker_coord0 > 0) {
    return EXIT_EARLY;
  }

  // Determine the value of the second Plucker coordinate from edge 1
  double plucker_coord1 =
    plucker_edge_test(vertices[1], vertices[2], raya, rayb);

  // If orientation is set, confirm that sign of plucker_coordinate indicate
  // correct orientation of intersection
  if (orientation) {
    if ((*orientation) * plucker_coord1 > 0) {
      return EXIT_EARLY;
    }
    // If the orientation is not specified, all plucker_coords must be the same
    // sign or zero.
  } else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) ||
             (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
    return EXIT_EARLY;
  }

  // Determine the value of the third Plucker coordinate from edge 2
  double plucker_coord2 =
    plucker_edge_test(vertices[2], vertices[0], raya, rayb);

  // If orientation is set, confirm that sign of plucker_coordinate indicate
  // correct orientation of intersection
  if (orientation) {
    if ((*orientation) * plucker_coord2 > 0) {
      return EXIT_EARLY;
    }
    // If the orientation is not specified, all plucker_coords must be the same
    // sign or zero.
  } else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) ||
             (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) ||
             (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
    return EXIT_EARLY;
  }

  // check for coplanar case to avoid dividing by zero
  if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
    return EXIT_EARLY;
  }

  // get the distance to intersection
  const double inverse_sum =
    1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
  assert(0.0 != inverse_sum);

  const dp::vec3 intersection(plucker_coord0 * inverse_sum * vertices[2] +
                              plucker_coord1 * inverse_sum * vertices[0] +
                              plucker_coord2 * inverse_sum * vertices[1]);

  // To minimize numerical error, get index of largest magnitude direction.
  int idx = 0;
  double max_abs_dir = 0;
  for (unsigned int i = 0; i < 3; ++i) {
    if (dp::abs(direction[i]) > max_abs_dir) {
      idx = i;
      max_abs_dir = dp::abs(direction[i]);
    }
  }

  dist_out = (intersection[idx] - origin[idx]) / direction[idx];

  // is the intersection within distance limits?
  if ((nonneg_ray_len && nonneg_ray_len < dist_out) ||  // intersection is beyond positive limit
      (neg_ray_len && *neg_ray_len >= dist_out) ||      // intersection is behind negative limit
      (!neg_ray_len && 0 > dist_out))                    // unless neg_ray_len used, don't allow negative distances
  {
    return EXIT_EARLY;
  }

  return {true, dist_out};
}

} // namespace xdg

#endif // include guard