#ifndef XDG_TETRAHEDRON_INTERSECT_H
#define XDG_TETRAHEDRON_INTERSECT_H


#include "xdg/embree/interface.h"
#include "xdg/vec3da.h"

namespace xdg
{
// Embree call back functions for element search
void VolumeElementBoundsFunc(RTCBoundsFunctionArguments* args);
void TetrahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TetrahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);
} // namespace xdg

#endif // include guard