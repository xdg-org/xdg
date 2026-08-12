
#ifndef XDG_BOUNDS_FUNCTIONS_H
#define XDG_BOUNDS_FUNCTIONS_H

#include "xdg/embree_interface.h"
#include "xdg/vec3da.h"

namespace xdg
{
  void VolumeElementBoundsFunc(RTCBoundsFunctionArguments* args);
  void SurfaceFaceBoundsFunc(RTCBoundsFunctionArguments* args);
} // namespace xdg

#endif