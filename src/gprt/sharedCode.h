#include "gprt.h"

#include "math/VectorTypes.h"

// extending the math namespace from GPRT/VectorTypes.h
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



#define AA 3 // used for antialiasing
#define EPSILON 2.2204460492503130808472633361816E-16
// #define FLT_EPSILON	1.19209290e-7F
// #define DBL_EPSILON	2.2204460492503131e-16

struct GPRTPrimitiveRef
{
  int id; // ID of the primitive
  int sense;
};

/* Inputs for each ray */
struct RayInput {
  float3 origin;
  float3 direction;
  int32_t* exclude_primitives; // Optional for excluding primitives
  uint32_t exclude_count;           // Number of excluded primitives
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

struct RayOutput 
{
  float distance;
  uint surf_id;
  float3 normal;
  uint piv; // Point in volume check result (0 for outside, 1 for inside)
};

struct dblRayOutput 
{
  double distance;
  uint surf_id;
  uint primitive_id;
  uint piv; // Point in volume check result (0 for outside, 1 for inside)
};

/* variables for the single precision triangle mesh geometry */
struct TrianglesGeomData {
  float3 *vertex; // vertex buffer
  uint3 *index;   // index buffer
  float3 *normals;
  uint id;        // surface id
  int forward_vol; 
  int reverse_vol;
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

struct RayGenData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  RayInput *ray;
  RayOutput *out;
};

struct dblRayGenData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  dblRayInput *ray;
  dblRayOutput *out;
  int orientation; // Orientation of the ray (0 for exiting, 1 for entering)
};

struct RayFireData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  RayInput ray;
  RayOutput out;
};

struct dblRayFireData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  dblRayInput ray;
  dblRayOutput out;
};

/* variables for the miss program */
struct MissProgData {
  float3 color0;
  float3 color1;
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */
struct PushConstants {
  float time;
  float3 scene_center;
  struct Camera {
    float radius;
    float3 pos;
    float3 dir_00;
    float3 dir_du;
    float3 dir_dv;
  } camera;
};

struct RayFirePushConstants {
  float dist_limit;
  int orientation;
};

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
  uint8_t hitOrientation;
};