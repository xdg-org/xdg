#ifndef XDG_EMBREE_TETRAHEDRON_CALLBACKS_H
#define XDG_EMBREE_TETRAHEDRON_CALLBACKS_H

#include "xdg/embree/embree_interface.h"

namespace xdg
{

// Embree call back functions for element search
void TetrahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TetrahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);

} // namespace xdg

#endif // include guard