#ifndef _XDG_DEVICE_RAY_H
#define _XDG_DEVICE_RAY_H

#include <cstddef>
#include <cstdint>

namespace xdg {

struct XDGRayHit {
  double origin[3];
  double direction[3];
  double t_min;
  double t_max;
  std::int32_t volume;

  double distance;
  std::int32_t surface;
  std::int32_t primitive;
  std::int32_t point_in_volume;
};

// Light wrapper for count and device id associated with pointer
struct XDGRayHitBuffer {
  XDGRayHit* data {nullptr};
  std::size_t count {0};
  int device_id {-1};
};

} // namespace xdg

#endif // include guard
