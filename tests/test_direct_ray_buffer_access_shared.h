#include "gprt.h"

#include "../include/xdg/gprt/ray.h"

struct ExternalRayParams {
  xdg::dblRay* xdgRays;
  double3* origins;
  double3* directions;
  uint num_rays;
  uint total_threads;
  int32_t* volume_mesh_ids;
  uint enabled;
};
