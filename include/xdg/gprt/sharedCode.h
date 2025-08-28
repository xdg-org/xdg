#include "gprt.h"


#ifndef __SLANG__ // only include this section for C++ compilation

#include "math/VectorTypes.h"
/*
  Needed so that math vector types match exactly with Slang definitions on host & device
  
  When I tried to use linalg's definitions I had issues with ambiguous definitions of vector
  types like float3, double3, between linalg and GPRT
  
  The following definitions extend the math utility definitions within GPRT but later versions of GPRT
  may include these directly
*/

namespace math
{
  using double1 = vector<double, 1>;
  using double2 = vector<double, 2>;
  using double3 = vector<double, 3>;
  using double4 = vector<double, 4>;
}

// Type aliases for convenience
using double1 = math::double1;
using double2 = math::double2;
using double3 = math::double3; 
using double4 = math::double4;

#endif // __SLANG__


#define AA 3 // used for antialiasing
#define EPSILON 2.2204460492503130808472633361816E-16
// #define FLT_EPSILON	1.19209290e-7F
// #define DBL_EPSILON	2.2204460492503131e-16

enum PointInVolume : uint { Outside = 0, Inside = 1 };

struct GPRTPrimitiveRef
{
  int id; // ID of the primitive
  int sense;
};

struct dblRayInput 
{
  double3 origin;
  double3 direction;
  double tMin; // Minimum distance for ray intersection
  double tMax; // Maximum distance for ray intersection
  int32_t* exclude_primitives; // Optional for excluding primitives
  uint32_t exclude_count;           // Number of excluded primitives
  uint8_t hitOrientation;
  uint volume_tree; // TreeID of the volume being queried
};

struct dblRayOutput 
{
  double distance;
  uint surf_id;
  uint primitive_id;
  uint piv; // Point in volume check result (0 for outside, 1 for inside)
};

/* variables for double precision triangle mesh geometry */
struct DPTriangleGeomData {
  double3 *vertex; // vertex buffer
  float3 *aabbs; // AABB buffer 
  uint3 *index;  // index buffer
  double3 *normals; // normals buffer
  uint surf_id;
  int2 vols;
  int forward_vol;
  int reverse_vol;
  dblRayInput *rayIn; // double precision rays
  uint8_t hitOrientation;
  int forward_tree; // TreeID of the forward volume
  int reverse_tree; // TreeID of the reverse volume
  GPRTPrimitiveRef* primitive_refs;
  uint num_faces; // Number of faces in the geometry
};

struct dblRayGenData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  dblRayInput *ray;
  dblRayOutput *out;
  int orientation; // Orientation of the ray (0 for exiting, 1 for entering)
};

struct dblRayFireData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  dblRayInput ray;
  dblRayOutput out;
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
  uint8_t hitOrientation;
};
