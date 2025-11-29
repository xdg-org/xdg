#ifndef _XDG_TRIANGLE_REF_H
#define _XDG_TRIANGLE_REF_H

#include "xdg/constants.h"
#include "xdg/embree/interface.h"

namespace xdg {

struct PrimitiveRef {
  MeshID primitive_id {ID_NONE};
};

void TriangleIntersectionFunc(RTCIntersectFunctionNArguments* args);
void TriangleBoundsFunc(RTCBoundsFunctionArguments* args);
void TriangleOcclusionFunc(RTCOccludedFunctionNArguments* args);
bool TriangleClosestFunc(RTCPointQueryFunctionArguments* args);

} // namespace xdg

#endif // include guard