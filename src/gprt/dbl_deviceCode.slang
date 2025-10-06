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
    // TODO : Should the rayOutput struct return the next volume ID for the ray back to the host

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
    rayDesc.TMin = float(record.ray[rayID].tMin); 
    rayDesc.TMax = float(record.ray[rayID].tMax); 

    SurfaceAccelerationStructure world = record.ray[rayID].volume_accel;

    // Pass the ray's origin and direction to the payload
    payload.distance = -1.0f;
    payload.surf_id = -1;
    payload.tlas = world;

    TraceRay(world, RAY_FLAG_NONE, 0xff, 0, 1, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.out[rayID].distance = payload.distance;
    record.out[rayID].surf_id = payload.surf_id;
    record.out[rayID].primitive_id = payload.primitive_id;
}

[shader("raygeneration")]
void point_in_volume(uniform dblRayGenData record, uniform DPTriangleGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = float3(record.ray[rayID].origin);
    rayDesc.Direction = float3(normalize(record.ray[rayID].direction));
    rayDesc.TMin = float(record.ray[rayID].tMin); 
    rayDesc.TMax = float(record.ray[rayID].tMax); 

    SurfaceAccelerationStructure world = record.ray[rayID].volume_accel;

    // Pass the ray's origin and direction to the payload
    payload.surf_id = -1;
    payload.tlas = world;
    payload.piv = xdg::PointInVolume::OUTSIDE; // Initialize point in volume check result to outside (0)
    
    TraceRay(world, RAY_FLAG_NONE, 0xff, 0, 1, rayDesc, payload);

    record.out.surf_id = payload.surf_id;
    record.out[rayID].piv = payload.piv; // Point in volume check result
}

// ------------------------------------------------- Compute Shaders -------------------------------------------------
/* A shader to compute and store AABB min/maxes in single precision using double precision coords*/
[shader("compute")]
[numthreads(1, 1, 1)]
void
populate_aabbs(uint3 DispatchThreadID: SV_DispatchThreadID, uniform DPTriangleGeomData record) {
    int primID = DispatchThreadID.x;
    int3 indices = record.index[primID];
    dp::vec3 A = record.vertex[indices[0]];
    dp::vec3 B = record.vertex[indices[1]];
    dp::vec3 C = record.vertex[indices[2]];
    dp::vec3 dpaabbmin = min(min(A, B), C);
    dp::vec3 dpaabbmax = max(max(A, B), C);
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

    if (rayID >= nRays) return;

    // Load vertices
    int3 indices = record.index[primID];
    dp::vec3 v0 = record.vertex[indices[0]];
    dp::vec3 v1 = record.vertex[indices[1]];
    dp::vec3 v2 = record.vertex[indices[2]];
    dp::vec3[3] vertices = { v0, v1, v2 };

    // Load rays
    dp::vec3 origin = record.rayIn[rayID].origin;
    dp::vec3 direction = record.rayIn[rayID].direction;
    double tMin = record.rayIn[rayID].tMin;
    double tMax = record.rayIn[rayID].tMax;

    // ----- Perform plucker intersection test -----

    const dp::vec3 raya = direction;
    const dp::vec3 rayb = dp::cross(direction, origin);

    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);
    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);
    
    // sign-consistency check
    if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) return;

    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);
    if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
        (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) return;

    // coplanar check
    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) return;

    const double inverse_sum = 1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
    const dp::vec3 intersection = dp::vec3(plucker_coord0 * inverse_sum * v2 +
                                         plucker_coord1 * inverse_sum * v0 +
                                         plucker_coord2 * inverse_sum * v1);

    int idx = 0;
    double max_abs_dir = 0;
    for(uint i = 0; i < 3; ++i) {
        if(abs(direction[i]) > max_abs_dir) {
            idx = i;
            max_abs_dir = abs(direction[i]);
        }
    }
    const double dist = (intersection[idx] - origin[idx]) / direction[idx];

    double t = dist;
    double u = plucker_coord2 * inverse_sum;
    double v = plucker_coord0 * inverse_sum;

    // Barycentric coords check
    if (u < 0.0 || v < 0.0 || (u + v) > 1.0) {
        t = -1.0;
    }

    // Check t range
    if (t > tMax) return;
    if (t < tMin) return;

    // ----- End of plucker intersection test -----

    DPAttribute attr;
    attr.f64t = t;

    float f32t = float(t);
    if (double(f32t) < t) f32t = next_after(f32t);

    dp::vec3 norm = record.normals[primID]; // recover double precision normal. TODO - Should we calculate from vertices instead?

    // sense adjustment of normal
    if (record.rayIn[rayID].volume_tree == record.reverse_tree)
    {
        norm = -norm;
    }

    double norm_dot_dir = dp::dot(norm, direction);
    uint hit_kind = norm_dot_dir < 0 ? HIT_KIND_TRIANGLE_FRONT_FACE
                                     : HIT_KIND_TRIANGLE_BACK_FACE;

    xdg::HitOrientation hitOrientation = record.rayIn[rayID].hitOrientation;

    if (orientation_cull(direction, norm, hitOrientation))
    {
        return;
    }

    for (int i = 0; i < record.rayIn[rayID].exclude_count; ++i)
    {
        if (record.rayIn[rayID].exclude_primitives[i] == global_prim_id) {
            return;
        }
    }
    attr.global_prim_id = global_prim_id;  
    ReportHit(f32t, hit_kind, attr);
}


// ------------------------------------------------- Helper functions -------------------------------------------------

bool orientation_cull(in dp::vec3 ray, in dp::vec3 normal, in HitOrientation orientation) {
    if (orientation == xdg::HitOrientation::ANY) return false; // No culling
    if (orientation == xdg::HitOrientation::EXITING) return dp::dot(ray, normal) < 0.0; // Cull exiting rays
    if (orientation == xdg::HitOrientation::ENTERING) return dp::dot(ray, normal) > 0.0; // Cull entering rays
    return false; // Default case, no culling
}

// Plucker coordinate
double plucker_edge_test(in dp::vec3 vertexa, in dp::vec3 vertexb, in dp::vec3 ray, in dp::vec3 ray_normal)
{
    double pip;
    const double near_zero = 10 * DBL_EPSILON;

    if (first(vertexa, vertexb))
    {
        dp::vec3 edge = vertexb - vertexa;
        dp::vec3 edge_normal = dp::cross(edge, vertexa);
        pip = dp::dot(ray, edge_normal) + dp::dot(ray_normal, edge);
    }
    else
    {
        dp::vec3 edge = vertexa - vertexb;
        dp::vec3 edge_normal = dp::cross(edge, vertexb);
        pip = dp::dot(ray, edge_normal) + dp::dot(ray_normal, edge);
        pip = -pip;
    }

    if (near_zero > abs(pip)) pip = 0.0;
    return pip;
}

/* Function to return the vertex with the lowest coordinates. To force the same
    ray-edge computation, the Plücker test needs to use consistent edge
    representation. This would be more simple with MOAB handles instead of
    coordinates... */
inline bool first(in dp::vec3 a, in dp::vec3 b)
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