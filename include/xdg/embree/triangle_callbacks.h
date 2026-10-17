#ifndef XDG_EMBREE_TRIANGLE_CALLBACKS_H
#define XDG_EMBREE_TRIANGLE_CALLBACKS_H

#include "xdg/embree/embree_interface.h"

namespace xdg {

void TriangleIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TriangleOcclusionFunc(RTCOccludedFunctionNArguments* args);
bool TriangleClosestFunc(RTCPointQueryFunctionArguments* args);

} // namespace xdg

#endif // XDG_EMBREE_TRIANGLE_CALLBACKS_H
