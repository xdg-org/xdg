#ifndef _XDG_GPRT_RAY_H
#define _XDG_GPRT_RAY_H

#include "gprt.h"
#include "../shared_enums.h"

namespace xdg {

struct dblRay 
{
  double3 origin;
  double3 direction;
  int32_t* exclude_primitives; // Optional for excluding primitives
  int32_t exclude_count;           // Number of excluded primitives
};

struct dblHit 
{
  double distance;
  int surf_id;
  int primitive_id;
  PointInVolume piv; // Point in volume check result (0 for outside, 1 for inside)
};

// TODO - Move this to its own header
struct DeviceRayHitBuffers {
  dblRay* rayDevPtr; // device pointer to ray buffers
  dblHit* hitDevPtr; // device pointer to hit buffers
  uint capacity = 0;
  
  // TODO - Renable once I figure out a way to make this slang safe 
  // bool valid() 
  // {
  //   return (rays != 0) && (hits != 0) && (capacity > 0);
  // }
};


}


#endif 