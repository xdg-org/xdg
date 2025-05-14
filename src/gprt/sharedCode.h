#include "gprt.h"

#define AA 3 // used for antialiasing

/* Inputs for each ray */
struct RayInput {
  float3 origin;
  float3 direction;
};

struct RayOutput 
{
  float distance;
  uint surfaceID;
  uint id;
};

/* variables for the triangle mesh geometry */
struct TrianglesGeomData {
  float3 *vertex; // vertex buffer
  uint3 *index;  // index buffer
  uint id;       // surface id
  uint2 vols;    // parent volumes
};

// /* variables for the triangle mesh geometry */
// struct DPTriangleGeomData {
//   double3 *vertex; // vertex buffer
//   uint3 *index;  // index buffer
//   double4 *dprays; // double precision rays
//   uint id;       // surface id
//   uint2 vols;    // parent volumes
//   uint fbSize; // framebuffer size
// };

struct RayGenData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  RayInput *ray;
  RayOutput *out;
};

struct RayFireData {
  uint* frameBuffer;                     // Optional for debugging or visuals
  SurfaceAccelerationStructure world;    // The top-level accel structure
  RayInput ray;
  RayOutput out;
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
  struct Camera {
    float3 pos;
    float3 dir_00;
    float3 dir_du;
    float3 dir_dv;
  } camera;
};

struct RayFirePushConstants {
  RayInput ray;
  RayOutput out;
  int dist_limit;
  int orientation;
};
