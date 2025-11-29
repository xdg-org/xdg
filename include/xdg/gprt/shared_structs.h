#ifndef XDG_GPRT_SHARED_STRUCTS_H
#define XDG_GPRT_SHARED_STRUCTS_H

#include "gprt.h"
#include "../shared_enums.h"
#include "ray.h"

struct GPRTPrimitiveRef
{
  int id; // ID of the primitive
  int sense;
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
  xdg::dblRay *ray; // double precision rays
  xdg::HitOrientation hitOrientation;
  int forward_tree; // TreeID of the forward volume
  int reverse_tree; // TreeID of the reverse volume
  GPRTPrimitiveRef* primitive_refs;
  int num_faces; // Number of faces in the geometry
};

struct dblRayGenData {
  xdg::dblRay *ray;
  xdg::dblHit *hit;
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
  SurfaceAccelerationStructure volume_accel; 
  int volume_tree;
  xdg::HitOrientation hitOrientation;
};

// TODO - Drop this in favour of exposing buffers directly
struct ExternalRayParams {
  xdg::dblRay* xdgRays;
  double3* origins;
  double3* directions;
  uint32_t num_rays;
  uint32_t total_threads;
};

#endif