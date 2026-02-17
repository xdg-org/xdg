#ifndef _XDG_GPRT_RAY_H
#define _XDG_GPRT_RAY_H

#include "gprt.h"
#include "../shared_enums.h"

/*
 * Double-precision ray and hit structures used by the GPRT backend.
 *
 * These types are not inherently GPRT-specific, but we keep them here for now
 * since GPRT is the only GPU backend. If another GPU backend is added, these
 * can be reused. Unifying them with the CPU/Embree types is possible, but may
 * not be worth the added complexity at this stage.
 */

namespace xdg {

struct dblRay 
{
  double3 origin;
  double3 direction;
  int volume_mesh_id; // MeshID of the volume this ray will be traced against
  uint enabled; // Flag to indicate if the ray is active
  // TODO - Implement exclude primtives functionality. Right now these are essentially just stubs.
  int32_t* exclude_primitives; // Optional for excluding primitives
  int32_t exclude_count; // Number of excluded primitives
};


// TODO - Should we define separate hit structs for PIV and ray-fire or do we think its better to keep them together?
struct dblHit 
{
  double distance;
  int surf_id;
  int primitive_id;
  PointInVolume piv; // Point in volume check result (0 for outside, 1 for inside)
};

}


#endif 
