

#include "xdg/geometry/plucker.h"


#include "xdg/constants.h"
#include "xdg/vec3da.h"

namespace xdg {

constexpr bool EXIT_EARLY = false;

double plucker_edge_test(const Position& vertexa, const Position& vertexb,
  const Position& ray, const Position& ray_normal)
{
  double pip;
  if (lower(vertexa, vertexb)) {
    const Position edge = vertexb - vertexa;
    const Position edge_normal = edge.cross(vertexa);
    pip = ray.dot(edge_normal) + ray_normal.dot(edge);
  } else {
    const Position edge = vertexa - vertexb;
    const Position edge_normal = edge.cross(vertexb);
    pip = ray.dot(edge_normal) + ray_normal.dot(edge);
    pip = -pip;
  }
  if (PLUCKER_ZERO_TOL > fabs(pip))
    pip = 0.0;
  return pip;
}

#include <cstdio>  // for // printf

bool plucker_ray_tri_intersect(const std::array<Position, 3> vertices,
                               const Position& origin,
                               const Direction& direction,
                               double& dist_out,
                               const double nonneg_ray_len,
                               const double* neg_ray_len,
                               const int* orientation)
{
  dist_out = INFTY;

  const Position raya = direction;
  const Position rayb = direction.cross(origin);

  // Determine the value of the first Plucker coordinate from edge 0
  double plucker_coord0 =
    plucker_edge_test(vertices[0], vertices[1], raya, rayb);
  // printf("plucker_coord0 = %g\n", plucker_coord0);

  // If orientation is set, confirm that sign of plucker_coordinate indicate
  // correct orientation of intersection
  if (orientation) {
    // printf("orientation = %d, orientation * plucker_coord0 = %g\n", *orientation, (*orientation) * plucker_coord0);
    if ((*orientation) * plucker_coord0 > 0) {
      // printf("Early exit: plucker_coord0 orientation check failed\n");
      return EXIT_EARLY;
    }
  }

  // Determine the value of the second Plucker coordinate from edge 1
  double plucker_coord1 =
    plucker_edge_test(vertices[1], vertices[2], raya, rayb);
  // printf("plucker_coord1 = %g\n", plucker_coord1);

  if (orientation) {
    // printf("orientation = %d, orientation * plucker_coord1 = %g\n", *orientation, (*orientation) * plucker_coord1);
    if ((*orientation) * plucker_coord1 > 0) {
      // printf("Early exit: plucker_coord1 orientation check failed\n");
      return EXIT_EARLY;
    }
  } else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) ||
             (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
    // printf("Early exit: plucker_coord0 and plucker_coord1 signs differ without orientation\n");
    return EXIT_EARLY;
  }

  // Determine the value of the third Plucker coordinate from edge 2
  double plucker_coord2 =
    plucker_edge_test(vertices[2], vertices[0], raya, rayb);
  // printf("plucker_coord2 = %g\n", plucker_coord2);
  if (orientation) {
    if ((*orientation) * plucker_coord2 > 0) {
      // printf("Early exit: plucker_coord2 orientation check failed\n");
      return EXIT_EARLY;
    }
  } else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) ||
             (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) ||
             (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
    // printf("Early exit: plucker_coords signs differ without orientation\n");
    return EXIT_EARLY;
  }

  // check for coplanar case to avoid dividing by zero
  if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
    // printf("Early exit: coplanar case, all plucker_coords are zero\n");
    return EXIT_EARLY;
  }

  // get the distance to intersection
  const double inverse_sum =
    1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
  assert(0.0 != inverse_sum);

  const Position intersection(plucker_coord0 * inverse_sum * vertices[2] +
                              plucker_coord1 * inverse_sum * vertices[0] +
                              plucker_coord2 * inverse_sum * vertices[1]);

  // To minimize numerical error, get index of largest magnitude direction.
  int idx = 0;
  double max_abs_dir = 0;
  for (unsigned int i = 0; i < 3; ++i) {
    if (fabs(direction[i]) > max_abs_dir) {
      idx = i;
      max_abs_dir = fabs(direction[i]);
    }
  }

  dist_out = (intersection[idx] - origin[idx]) / direction[idx];

  // is the intersection within distance limits?
  if ((nonneg_ray_len && nonneg_ray_len < dist_out) ||  // intersection is beyond positive limit
      (neg_ray_len && *neg_ray_len >= dist_out) ||      // intersection is behind negative limit
      (!neg_ray_len && 0 > dist_out))                    // unless neg_ray_len used, don't allow negative distances
  {
    // printf("Early exit: intersection out of range, dist_out = %g\n", dist_out);
    return EXIT_EARLY;
  }

  return true;
}


} // namespace xdg