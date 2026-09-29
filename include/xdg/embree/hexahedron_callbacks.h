#ifndef XDG_EMBREE_HEXAHEDRON_CALLBACKS_H
#define XDG_EMBREE_HEXAHEDRON_CALLBACKS_H

#include "xdg/embree/embree_interface.h"

namespace xdg {

// Embree call back functions for element search
void HexahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void HexahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);

} // namespace xdg

#endif // include guard
