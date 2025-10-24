#ifndef XDG_TETRAHEDRON_INTERSECT_H
#define XDG_TETRAHEDRON_INTERSECT_H


#include "xdg/vec3da.h"

namespace xdg
{
/**
 * @brief Determines if a point is inside or on the boundary of a tetrahedron
 * using Plücker coordinates.
 *
 * This function performs a containment test for a point relative to a
 * tetrahedron defined by four vertices. The ordering of these vertices is based
 * on the following reference: https://doi.org/10.1002/cnm.1237 where each
 * triangle is oriented counter-clockwise when viewed from the front face
 * (normals pointing outward with respect to the tetrahedron). It uses
 * barycentric coordinates to determine whether the point lies inside, on the
 *
 * It uses signed
 * volume tests (via `face_side_test`) to determine whether the point lies
 * inside, on the boundary, or outside the tetrahedron. If the point lies
 * exactly on a face of the tetrahedron, it is considered inside.
 *
 * @param point The position of the point to test.
 * @param v0 The position of the first vertex of the tetrahedron.
 * @param v1 The position of the second vertex of the tetrahedron.
 * @param v2 The position of the third vertex of the tetrahedron.
 * @param v3 The position of the fourth vertex of the tetrahedron.
 * @return `true` if the point is inside or on the boundary of the tetrahedron,
 * `false` otherwise.
 */
bool plucker_tet_containment_test(const Position& point,
                                  const Vertex& v0,
                                  const Vertex& v1,
                                  const Vertex& v2,
                                  const Vertex& v3);

// Embree call back functions for element search
void VolumeElementBoundsFunc(RTCBoundsFunctionArguments* args);
void TetrahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TetrahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);

} // namespace xdg

#endif // include guard