#include "gprt.h"

#define AA 3 // used for antialiasing

/* variables available to all programs */

/* variables for the triangle mesh geometry */
struct TrianglesGeomData {
  float3 *vertex; // vertex buffer
  uint3 *index;  // index buffer
  uint id;       // surface id
  uint2 vols;    // parent volumes
};

struct RayGenData {
  uint *frameBuffer;
  SurfaceAccelerationStructure world;
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