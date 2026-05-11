#include "gprt.h"
#include "../shared_enums.h"
#include "../geometry/dp_math.h"


namespace xdg {
  static const uint RT_SURFACE_RAY_INDEX = 0; // SBT ray index for surface ray queries
  static const uint RT_VOLUME_RAY_INDEX = 1; // SBT ray index for volumetric ray queries 
  static const uint RT_NUM_RAY_TYPES = 2; // total number of ray types (surface and volume)
  static const uint RT_SURFACE_MISS_INDEX = 0; // SBT ray index for surface ray miss shader
  static const uint RT_VOLUME_MISS_INDEX = 1; // SBT ray index for
}
struct GPRTPrimitiveRef
{
  int id; // ID of the primitive
  int sense;
};

struct dblRay 
{
  double3 origin;
  double3 direction;
  double tMin; // Minimum distance for ray intersection
  double tMax; // Maximum distance for ray intersection
  int32_t* exclude_primitives; // Optional for excluding primitives
  int32_t exclude_count;           // Number of excluded primitives
  xdg::HitOrientation hitOrientation;
  int volume_tree; // TreeID of the volume being queried
  SurfaceAccelerationStructure volume_accel_surf; // The volume accel for surface acceleration structure
  SolidAccelerationStructure volume_accel_solid; // The volume accel for solid acceleration structure
};

struct dblHit 
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
  dblRay *ray; // double precision rays
  xdg::HitOrientation hitOrientation;
  int forward_tree; // TreeID of the forward volume
  int reverse_tree; // TreeID of the reverse volume
  GPRTPrimitiveRef* primitive_refs;
  int num_faces; // Number of faces in the geometry
};

struct DPTetrahedronGeomData {
  double3 *vertex; // vertex buffer
  float3 *aabbs; // AABB buffer 
  uint4 *index;  // index buffer
  int32_t vol_id;
  dblRay *ray; // double precision rays
  xdg::HitOrientation hitOrientation;
  GPRTPrimitiveRef* primitive_refs;
  int num_tets; // Number of tetrahedra in the geometry
};

struct dblRayGenData {
  dblRay *ray;
  dblHit *hit;
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
};
