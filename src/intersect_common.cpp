
#include <algorithm>

#include "xdg/intersect_common.h"

namespace xdg {

bool orientation_cull(const Direction& ray_dir, const Direction& normal, HitOrientation orientation)
{
  if (orientation == HitOrientation::ANY) return false;

  double dot_prod = ray_dir.dot(normal);
  if (orientation == HitOrientation::EXITING && dot_prod < 0.0) {
    return true;
  }
  else if (orientation == HitOrientation::ENTERING && dot_prod >= 0.0) {
    return true;
  }
  return false;
}

bool primitive_mask_cull(RTCDualRayHit* rayhit, int primID) {
  if (!rayhit->ray.exclude_primitives) return false;

  RTCSurfaceDualRay& ray = rayhit->ray;

  // if the primitive mask is set, cull if the primitive is not in the mask
  return std::find(ray.exclude_primitives->begin(), ray.exclude_primitives->end(), primID) != ray.exclude_primitives->end();
}

} // namespace xdg