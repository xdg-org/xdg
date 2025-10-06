#include "gprt.h"
#include "../shared_enums.h"
#include "xdg/geometry/dp_math.h"

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
  int32_t exclude_count;           // Number of excluded primitives
  xdg::HitOrientation hitOrientation;
  int volume_tree; // TreeID of the volume being queried
  SurfaceAccelerationStructure volume_accel; // The volume accel 
};

struct dblRayOutput 
{
  double distance;
  int surf_id;
  int primitive_id;
  xdg::PointInVolume piv; // Point in volume check result (0 for outside, 1 for inside)
};

/* variables for double precision triangle mesh geometry */
struct DPTriangleGeomData {
  double3 *vertex; // vertex buffer
  float3 *aabbs; // AABB buffer 
  uint3 *index;  // index buffer
  double3 *normals; // normals buffer
  int surf_id;
  int2 vols;
  int forward_vol;
  int reverse_vol;
  dblRayInput *rayIn; // double precision rays
  xdg::HitOrientation hitOrientation;
  int forward_tree; // TreeID of the forward volume
  int reverse_tree; // TreeID of the reverse volume
  GPRTPrimitiveRef* primitive_refs;
  int num_faces; // Number of faces in the geometry
};

struct dblRayGenData {
  dblRayInput *ray;
  dblRayOutput *out;
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
};
