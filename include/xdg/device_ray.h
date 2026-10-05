#ifndef XDG_DEVICE_RAY_H
#define XDG_DEVICE_RAY_H

#ifndef __SLANG__
#include <cstddef>
#include <cstdint>
#endif

namespace xdg {

// Slang does not support STL types so we typedef depending on whether compiling for C++ or Slang
#ifdef __SLANG__
typedef int int32;
#else
using int32 = std::int32_t;
#endif

// TODO - Try converting to SoA rather than AoS for better memory coalescing on GPU. This would require a more complex buffer management system, but could yield performance improvements for large ray batches.
//! C++/Slang compilable struct representing a ray and its associated hit information.
struct XDGRayHit {
  double origin[3];
  double direction[3];
  double t_min;
  double t_max;
  int32 volume;
  int32 last_hit_primitive = -1;

  double distance;
  int32 surface;
  int32 primitive;
  int32 point_in_volume;
  int32 next_volume;
  int32 boundary_condition;
  double normal[3];
};

#ifndef __SLANG__

// Static assertions to ensure that the sizes of the structures are as expected
// between host and device code. This is important for ensuring that the data layout
// is consistent across different platforms and compilers, especially when using
// GPU acceleration or other device-specific features.
static_assert(sizeof(int32) == 4);
static_assert(offsetof(XDGRayHit, origin) == 0);
static_assert(offsetof(XDGRayHit, volume) == 64);
static_assert(offsetof(XDGRayHit, distance) == 72);
static_assert(offsetof(XDGRayHit, normal) == 104);
static_assert(sizeof(XDGRayHit) == 128);

//! Struct representing a device buffer of XDGRayHit records.
//! This struct is entirely host side and is used to manage device memory for ray-hit records.
struct XDGRayHitBuffer {
  XDGRayHit* data {nullptr};
  std::size_t count {0};
  int device_id {-1};
  void* native_handle {nullptr};
};

#endif // ifndef __SLANG__

} // namespace xdg

#endif // XDG_DEVICE_RAY_H
