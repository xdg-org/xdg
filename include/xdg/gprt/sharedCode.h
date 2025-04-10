#include "gprt/gprt.h"

/* variables for the triangle mesh geometry */
struct DPTriangleData
{
#ifdef GPRT_CPU_ONLY
  typedef double3 vertex_type;
#endif
  /*! array/buffer of vertex indices */
  uint32_t* index; // vec3f*
  /*! array/buffer of vertex positions */
  uint32_t* vertex; // float *
  /*! array/buffer of AABBs */
  uint32_t* aabbs;
  /*! array/buffer of double precision rays */
  uint32_t* dpRays;
  /* ID of the surface (negative if triangle connecivity has been reversed)*/
  int32_t id;
  /*! acceleration data structure for the volume on the front face */
  int32_t ff_vol;
  /*! acceleration data structure for the volume on the back face */
  int32_t bf_vol;
  /*! frame buffer size */
  int2 fbSize;
  /*! volume on the forward (front) and reverse (back) side of the surface */
  int2 vols;
};

struct SPTriangleData
{
#ifdef GPRT_CPU_ONLY
  typedef float3 vertex_type;
#endif
  /*! array/buffer of vertex indices */
  uint32_t* index; // vec3f*
  /*! array/buffer of vertex positions */
  uint32_t* vertex; // float *
  /* ID of the surface (negative if triangle connecivity has been reversed)*/
  int32_t id;
  /*! acceleration data structure for the volume on the front face */
  int32_t ff_vol;
  /*! acceleration data structure for the volume on the back face */
  int32_t bf_vol;
  /*! volume on the forward and reverse side of the surface */
  int2 vols;
};

struct RayGenData
{
  uint32_t* accumPtr;
  uint32_t* fbPtr;
  uint32_t* dpRays;

  GPRTTexture guiTexture;

  // a relative unit for delta tracking, similar to "dt"
  float unit;
  uint32_t frameID;
  uint32_t numVolumes;
  uint32_t maxVolID;
  uint32_t graveyardID;
  uint32_t complementID;
  // colormap for visualization
  GPRTTexture surfaceColormap;
  GPRTTexture ddaColormap;
  GPRTSampler colormapSampler;

  int2 fbSize;

  uint32_t moveOrigin;

  GPRTAccel world;
  uint32_t* partTrees; // GPRTAccel*

  uint32_t* ddaGrid;
  uint3 gridDims;

  float3 aabbMin;
  float3 aabbMax;

  struct {
    float3 pos;
    float3 dir_00;
    float3 dir_du;
    float3 dir_dv;
  } camera;
};

/* variables for the miss program */
struct MissProgData
{
  float3  color0;
  float3  color1;
};

#ifdef GPRT_DEVICE

#define MAX_DEPTH 100

#define EPSILON 2.2204460492503130808472633361816E-16
#define FLT_EPSILON	1.19209290e-7F
#define DBL_EPSILON	2.2204460492503131e-16

float4 over(float4 a, float4 b) {
  float4 result;
  result.a = a.a + b.a * (1.f - a.a);
  result.rgb = (a.rgb * a.a + b.rgb * b.a * (1.f - a.a)) / result.a;
  return result;
}

struct [raypayload] Payload
{
  // .y = moving into that volume
  int2 vol_ids       : read(caller) : write(miss, closesthit);
  int surf_id        : read(caller) : write(closesthit);
  float hitDistance  : read(caller) : write(closesthit);
  int next_vol       : read(caller) : write(closesthit);
};

#endif