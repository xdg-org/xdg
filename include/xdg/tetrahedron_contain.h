#ifndef XDG_TETRAHEDRON_INTERSECT_H
#define XDG_TETRAHEDRON_INTERSECT_H


#include "xdg/vec3da.h"

namespace xdg
{

/**
 * @brief Computes the side of a point relative to a triangle face.
 *
 * The signed volume is calculated using the scalar triple product of vectors formed
 * by the input positions. A positive volume indicates a specific orientation of the
 * tetrahedron, while a negative volume indicates the opposite orientation.
 *
 * @param point The query location.
 * @param v0 The first vertex of the triangle face.
 * @param v1 The second vertex of the triangle face.
 * @param v2 The third vertex of the triangle face.
 * @return The signed volume of the triangle face.
 */
double face_side_test(const Position& point, const Position& v0, const Position& v1, const Position& v2);


/**
 * @brief Determines if a point is inside or on the boundary of a tetrahedron using Plücker coordinates.
 *
 * This function performs a containment test for a point relative to a tetrahedron defined by four vertices.
 * It uses signed volume tests (via `face_side_test`) to determine whether the point lies inside, on the boundary,
 * or outside the tetrahedron. If the point lies exactly on a face of the tetrahedron, it is considered inside.
 *
 * @param point The position of the point to test.
 * @param v0 The position of the first vertex of the tetrahedron.
 * @param v1 The position of the second vertex of the tetrahedron.
 * @param v2 The position of the third vertex of the tetrahedron.
 * @param v3 The position of the fourth vertex of the tetrahedron.
 * @return `true` if the point is inside or on the boundary of the tetrahedron, `false` otherwise.
 */
bool plucker_tet_containment_test(const Position& point,
  const Position& v0,
  const Position& v1,
  const Position& v2,
  const Position& v3);

void VolumeElementBoundsFunc(RTCBoundsFunctionArguments* args);
void TetrahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TetrahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);

} // namespace xdg

#endif // include guard