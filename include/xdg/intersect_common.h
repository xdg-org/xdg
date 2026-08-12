#ifndef XDG_INTERSECT_COMMON_H
#define XDG_INTERSECT_COMMON_H

#include "xdg/shared_enums.h"
#include "xdg/ray.h"

namespace xdg {

  bool orientation_cull(const Direction& ray_dir, const Direction& normal, HitOrientation orientation);

  bool primitive_mask_cull(RTCDualRayHit* rayhit, int primID);

} // namespace xdg

#endif