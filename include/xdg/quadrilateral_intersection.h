#ifndef XDG_QUADRILATERAL_INTERSECTION_H
#define XDG_QUADRILATERAL_INTERSECTION_H

#include "xdg/embree_interface.h"

namespace xdg {

void QuadIntersectionFunction(RTCIntersectFunctionNArguments* args);
void QuadBoundsFunction(RTCBoundsFunctionArguments* args);
void QuadOcclusionFunction(RTCOccludedFunctionNArguments* args);
bool QuadClosestFunction(RTCPointQueryFunctionArguments* args);

// Dispatches to the appropriate closest function based on surface element type.
bool SurfaceClosestFunc(RTCPointQueryFunctionArguments* args);

} // namespace xdg

#endif // XDG_QUADRILATERAL_INTERSECTION_H
