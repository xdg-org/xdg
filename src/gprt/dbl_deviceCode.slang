#include "../../include/xdg/gprt/shared_structs.h"

[[vk::push_constant]]
dblRayFirePushConstants PC;

struct RayFirePayload {
    double distance;    // Distance to intersection
    int surf_id;     // ID of the surface hit
    SurfaceAccelerationStructure tlas;
    int primitive_id; // ID of the primitive hit
    xdg::PointInVolume piv; // Point in volume check (0 for outside, 1 for inside) 
};


struct DPAttribute
{
  double f64t; // double precision hit distance
  int global_prim_id;
};


[shader("closesthit")]
void ray_fire_hit(uniform DPTriangleGeomData record, inout RayFirePayload payload, in DPAttribute attr) {
    // Distance from the ray origin to the hit point
    uint hit_kind = HitKind();
    uint rayID = DispatchRaysIndex().x;

    // There is some logic for handling next volumes inside the h5m-reader which I could make use of too
    // TODO : Should the dblHit struct return the next volume ID for the ray back to the host

    payload.piv = (hit_kind == HIT_KIND_TRIANGLE_FRONT_FACE) 
    ? xdg::PointInVolume::OUTSIDE
    : xdg::PointInVolume::INSIDE; 

    int instanceID = InstanceID();

    payload.distance = attr.f64t;
    payload.surf_id = record.surf_id;
    payload.primitive_id = attr.global_prim_id;
}

[shader("miss")]
void ray_fire_miss(inout RayFirePayload payload) {
    // Set the miss payload to default values
    payload.distance = -1.0f;
    payload.surf_id = -1;
    payload.primitive_id = -1;
}

// This ray generation program will kick off the ray tracing process,
// generating rays and tracing them into the world.
[shader("raygeneration")]
void ray_fire(uniform dblRayGenData record, uniform DPTriangleGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = float3(record.ray[rayID].origin);
    rayDesc.Direction = normalize(float3(record.ray[rayID].direction));
    rayDesc.TMin = float(PC.tMin); 
    rayDesc.TMax = float(PC.tMax); 

    SurfaceAccelerationStructure world = PC.volume_accel;

    // Pass the ray's origin and direction to the payload
    payload.distance = -1.0f;
    payload.surf_id = -1;
    payload.tlas = world;

    TraceRay(world, RAY_FLAG_NONE, 0xff, 0, 1, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.hit[rayID].distance = payload.distance;
    record.hit[rayID].surf_id = payload.surf_id;
    record.hit[rayID].primitive_id = payload.primitive_id;
}

[shader("raygeneration")]
void point_in_volume(uniform dblRayGenData record, uniform DPTriangleGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = float3(record.ray[rayID].origin);
    rayDesc.Direction = float3(normalize(record.ray[rayID].direction));
    rayDesc.TMin = float(PC.tMin); 
    rayDesc.TMax = float(PC.tMax); 

    SurfaceAccelerationStructure world = PC.volume_accel;

    // Pass the ray's origin and direction to the payload
    payload.surf_id = -1;
    payload.tlas = world;
    payload.piv = xdg::PointInVolume::OUTSIDE; // Initialize point in volume check result to outside (0)
    
    TraceRay(world, RAY_FLAG_NONE, 0xff, 0, 1, rayDesc, payload);

    record.hit[rayID].surf_id = payload.surf_id;
    record.hit[rayID].piv = payload.piv; // Point in volume check result
}

// ------------------------------------------------- Compute Shaders -------------------------------------------------
/* A shader to compute and store AABB min/maxes in single precision using double precision coords*/
[shader("compute")]
[numthreads(64, 1, 1)]
void populate_aabbs(uint3 DispatchThreadID: SV_DispatchThreadID, uniform DPTriangleGeomData record) {
    int primID = DispatchThreadID.x;

    // guard against more threads than primitives
    if (primID >= record.num_faces)
        return;

    int3 indices = record.index[primID];
    double3 A = record.vertex[indices[0]];
    double3 B = record.vertex[indices[1]];
    double3 C = record.vertex[indices[2]];
    double3 dpaabbmin = min(min(A, B), C);
    double3 dpaabbmax = max(max(A, B), C);
    float3 fpaabbmin = float3(dpaabbmin - float3(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON));
    float3 fpaabbmax = float3(dpaabbmax + float3(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON));

    record.aabbs[2 * primID] = fpaabbmin;
    record.aabbs[2 * primID + 1] = fpaabbmax;
}

// ------------------------------------------------ CUSTOM INTERSECTION SHADERS ------------------------------------------------


/* 1D ray generation intersection with a double precision triangle using the Plucker intersection algorithm*/
[shader("intersection")]
void DPTrianglePluckerIntersection(uniform DPTriangleGeomData record)
{
    int primID = PrimitiveIndex();
    int global_prim_id = record.primitive_refs[primID].id;

    uint rayID = DispatchRaysIndex().x;
    uint nRays = DispatchRaysDimensions().x;
    uint flags = RayFlags();

    if (rayID >= nRays) {
        return;
    }

    bool useOrientation = false;
    int orientation = 0;
    if ((flags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0) {
        orientation = -1;
        useOrientation = true;
    }
    else if ((flags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0) {
        orientation = 1;
        useOrientation = true;
    }

    int3 indices = record.index[primID];
    double3 v0 = record.vertex[indices[0]];
    double3 v1 = record.vertex[indices[1]];
    double3 v2 = record.vertex[indices[2]];

    double3 origin = record.ray[rayID].origin;
    double3 direction = record.ray[rayID].direction;

    double tMin = PC.tMin;
    double tMax = PC.tMax;

    const double3 raya = direction;
    const double3 rayb = cross(direction, origin);

    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);
    if (useOrientation && orientation * plucker_coord0 > 0) {
        return;
    }

    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);
    if (useOrientation && orientation * plucker_coord1 > 0) {
        return;
    }
    else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
        return;
    }

    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);
    if (useOrientation && orientation * plucker_coord2 > 0) {
        return;
    }
    else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
        return;
    }

    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
        return;
    }

    const double inverse_sum = 1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
    const double3 intersection = double3(plucker_coord0 * inverse_sum * v2 +
                                         plucker_coord1 * inverse_sum * v0 +
                                         plucker_coord2 * inverse_sum * v1);

    int idx = 0;
    double max_abs_dir = 0;
    for(uint i = 0; i < 3; ++i) {
        if(abs(direction[i]) > max_abs_dir) {
            idx         = i;
            max_abs_dir = abs(direction[i]);
        }
    }
    const double dist = (intersection[idx] - origin[idx]) / direction[idx];

    double t = dist;
    double u = plucker_coord2 * inverse_sum;
    double v = plucker_coord0 * inverse_sum;


    if(u < 0.0 || v < 0.0 || (u + v) > 1.0) {
        t = -1.0;
    }
    if (t > tMax) {
        return;
    }
    if (t < tMin) {
        return;
    }

    DPAttribute attr;
    attr.f64t = t;

    float f32t = float(t);
    if (double(f32t) < t) f32t = next_after(f32t);

    double3 norm = record.normals[primID]; // recover double precision normal. TODO - Should we calculate from vertices instead?

    // sense adjustment of normal
    if (PC.volume_tree == record.reverse_tree)
    {
        norm = -norm;
    }

    double norm_dot_dir = dot(norm, direction);
    uint hit_kind = norm_dot_dir < 0 ? HIT_KIND_TRIANGLE_FRONT_FACE
                                     : HIT_KIND_TRIANGLE_BACK_FACE;

    xdg::HitOrientation hitOrientation = PC.hitOrientation;

    if (orientation_cull(direction, norm, hitOrientation))
    {
        return;
    }

    for (int i = 0; i < record.ray[rayID].exclude_count; ++i)
    {
        if (record.ray[rayID].exclude_primitives[i] == global_prim_id) {
            return;
        }
    }
    attr.global_prim_id = global_prim_id;  
    ReportHit(f32t, hit_kind, attr);
}


// ------------------------------------------------- Helper functions -------------------------------------------------

bool orientation_cull(in double3 ray, in double3 normal, in xdg::HitOrientation orientation) {
    if (orientation == xdg::HitOrientation::ANY) return false; // No culling
    if (orientation == xdg::HitOrientation::EXITING) return dot(ray, normal) < 0.0; // Cull exiting rays
    if (orientation == xdg::HitOrientation::ENTERING) return dot(ray, normal) > 0.0; // Cull entering rays
    return false; // Default case, no culling
}

// Plucker coordinate
double plucker_edge_test(in double3 vertexa, in double3 vertexb, in double3 ray, in double3 ray_normal)
{
    double pip;
    const double near_zero = 10 * DBL_EPSILON;

    if (first(vertexa, vertexb))
    {
        double3 edge = vertexb - vertexa;
        double3 edge_normal = cross(edge, vertexa);
        pip = dot(ray, edge_normal) + dot(ray_normal, edge);
    }
    else
    {
        double3 edge = vertexa - vertexb;
        double3 edge_normal = cross(edge, vertexb);
        pip = dot(ray, edge_normal) + dot(ray_normal, edge);
        pip = -pip;
    }

    if (near_zero > abs(pip)) pip = 0.0;
    return pip;
}

/* Function to return the vertex with the lowest coordinates. To force the same
    ray-edge computation, the Plücker test needs to use consistent edge
    representation. This would be more simple with MOAB handles instead of
    coordinates... */
inline bool first(in double3 a, in double3 b)
{
    if (a[0] < b[0]) return true;

    if (a[0] == b[0] && a[1] < b[1]) return true;

    if (a[1] == b[1] && a[2] < b[2]) return true;

    return false;
}

float next_after(float a) {
  uint a_ = asuint(a);
  if (a < 0) {
    a_--;
  } else {
    a_++;
  }
  return asfloat(a_);
}