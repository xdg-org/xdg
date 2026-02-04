#ifndef XDG_GPRT_SHARED_STRUCTS_H
#define XDG_GPRT_SHARED_STRUCTS_H

#include "gprt.h"
#include "../shared_enums.h"
#include "ray.h"

struct GPRTPrimitiveRef
{
  int id; // ID of the primitive
};


/* variables for double precision triangle mesh geometry */
struct DPTriangleGeomData {
  double3 *vertex; // vertex buffer
  float3 *aabbs; // AABB buffer 
  uint3 *index;  // index buffer
  double3 *normals; // normals buffer
  int surf_id;
  int* meshid_to_sense; // MeshID -> sense (+1 forward, -1 reverse)
  xdg::dblRay *ray; // double precision rays
  xdg::HitOrientation hitOrientation;
  GPRTPrimitiveRef* primitive_refs;
  int num_faces; // Number of faces in the geometry
};

struct dblRayGenData {
  xdg::dblRay *ray;
  xdg::dblHit *hit;
  SurfaceAccelerationStructure* meshid_to_accel_address; // MeshID->TLAS address table to recover volume to trace against
};

/* A small structure of constants that can change every frame without rebuilding the
  shader binding table. (must be 128 bytes or less) */

struct dblRayFirePushConstants {
  double tMax;
  double tMin;
  xdg::HitOrientation hitOrientation;
};

#endif
