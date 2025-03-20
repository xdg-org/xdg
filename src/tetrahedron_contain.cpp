#include "xdg/vec3da.h"

namespace xdg
{


double face_side_test(const Position& point, const Position& v0, const Position& v1, const Position& v2) {
  // Compute the face normal using the cross product of two edge vectors
  auto face_normal = cross(v1 - v0, v2 - v0).normalize();

  auto vec_a = v0 - point;
  auto vec_b = v1 - point;
  auto vec_c = v2 - point;

  auto cross_product = cross(vec_a, vec_b).normalize();
  return dot(cross_product, vec_c);
}

bool plucker_tet_containment_test(const Position& point,
                                  const Position& v0,
                                  const Position& v1,
                                  const Position& v2,
                                  const Position& v3) {
  // Compute the first signed volume
  double sv0 = face_side_test(point, v0, v2, v1);
  if (sv0 == 0.0) return true; // If exactly on a face, consider it inside
  bool is_positive = (sv0 > 0);

  // Compute the next signed volume and check sign consistency
  double sv1 = face_side_test(point, v0, v1, v3);
  if (sv1 == 0.0) return true; // If on a face
  if ((sv1 > 0) != is_positive) return false; // Early exit if signs differ

  // Compute the next signed volume and check sign consistency
  double sv2 = face_side_test(point, v0, v3, v2);
  if (sv2 == 0.0) return true; // If on a face
  if ((sv2 > 0) != is_positive) return false; // Early exit if signs differ

  // Compute the final signed volume and check sign consistency
  double sv3 = face_side_test(point, v1, v2, v3);
  if (sv3 == 0.0) return true; // If on a face
  if ((sv3 > 0) != is_positive) return false; // Early exit if signs differ

  return true; // All volumes have the same sign → point is inside
}

} // namespace xdg