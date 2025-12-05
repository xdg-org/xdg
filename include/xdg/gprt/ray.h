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

}


#endif 