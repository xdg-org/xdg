#include "sharedCode.h"

[[vk::push_constant]]
PushConstants pc;

[[vk::push_constant]]
RayFirePushConstants rayFirePC;

struct Payload {
    float3 color;
};

struct RayFirePayload {
    double distance;    // Distance to intersection
    int surf_id;     // ID of the surface hit
    int2 vol_ids;       // ID of the volume the ray is in
    SurfaceAccelerationStructure tlas;
    int next_vol;      // ID of the next volume
    double3 normal;
    uint hit_kind; // Kind of hit (front or back face)
};

struct DPAttribute
{
  double hitDistance; // barycentrics
};


[shader("closesthit")]
void ray_fire_hit(uniform DPTriangleGeomData record, inout RayFirePayload payload, in DPAttribute attr) {
    // Distance from the ray origin to the hit point
    uint hit_kind = HitKind();
    uint rayID = DispatchRaysIndex().x;


    if (hit_kind == HIT_KIND_TRIANGLE_FRONT_FACE){
        payload.vol_ids.x = record.vols[0];
        payload.vol_ids.y = record.vols[1];
        payload.next_vol = record.reverse_vol;
    }
    else {
        payload.vol_ids.x = record.vols[1];
        payload.vol_ids.y = record.vols[0];
        payload.next_vol = record.forward_vol;
    }
    int instanceID = InstanceID();

    printf("Primitive ID: %i, prim_MeshID: %i, instance_MeshID: %i\n", PrimitiveIndex(), record.prim_ids[PrimitiveIndex()], instanceID);
    payload.distance = attr.hitDistance;
    payload.surf_id = record.surf_id;
    payload.normal = record.normals[PrimitiveIndex()];
    payload.hit_kind = hit_kind;
}

[shader("miss")]
void ray_fire_miss(inout RayFirePayload payload) {
    // Set the miss payload to default values
    payload.distance = -1.0f;
    payload.surf_id = ~0u;
    payload.normal = {0.0, 0.0, 0.0};
    printf("Miss shader called!\n");
}

// This ray generation program will kick off the ray tracing process,
// generating rays and tracing them into the world.
[shader("raygeneration")]
void ray_fire(uniform dblRayGenData record, uniform DPTriangleGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    printf("Ray Generation Shader called with :");
    printf("Ray Origin: (%f, %f, %f)\n", record.ray.origin.x, record.ray.origin.y, record.ray.origin.z);
    printf("Ray Direction: (%f, %f, %f)\n", record.ray.direction.x, record.ray.direction.y, record.ray.direction.z);
    printf("Excluded Primitives.size(): %i \n", record.ray.exclude_count);

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = float3(record.ray[rayID].origin);
    rayDesc.Direction = normalize(float3(record.ray[rayID].direction));
    rayDesc.TMin = 1e-4f; // non-zero to avoid self-intersection
    rayDesc.TMax = rayFirePC.dist_limit; // distance limit from push constants

    record.ray[rayID].tMin = rayDesc.TMin;
    record.ray[rayID].tMax = rayDesc.TMax;

    // Pass the ray's origin and direction to the payload
    payload.distance = -1.0f;
    payload.surf_id = ~0u;
    payload.tlas = record.world;


    // uint rayFlagCull = (rayFirePC.orientation == 1)
    //                ? RAY_FLAG_CULL_FRONT_FACING_TRIANGLES
    //                : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;


    // printf("Distance Limit: %f\n", rayFirePC.dist_limit);
    // printf("Orientation: %u\n", rayFirePC.orientation);
    // printf("TMax before TraceRay: %f\n PC.dist_limit: %f\n", rayDesc.TMax, rayFirePC.dist_limit);
    // printf("Exclude_primitive size: %i \n", record.ray.exclude_count);
    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 1, 0, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.out[rayID].distance = payload.distance;
    record.out[rayID].surf_id = payload.surf_id;
}

[shader("raygeneration")]
void point_in_volume(uniform dblRayGenData record, uniform DPTriangleGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    printf("Point in Volume Ray Generation Shader called\n");
    // printf("Ray Origin: (%f, %f, %f)\n", record.ray.origin.x, record.ray.origin.y, record.ray.origin.z);
    // printf("Ray Direction: (%f, %f, %f)\n", record.ray.direction.x, record.ray.direction.y, record.ray.direction.z);
    // printf("Excluded Primitives.size(): %i \n", record.ray.exclude_count);

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = float3(record.ray[rayID].origin);
    rayDesc.Direction = float3(normalize(record.ray[rayID].direction));
    rayDesc.TMin = 1e-4f; // non-zero to avoid self-intersection
    rayDesc.TMax = 1e30f;

    // Pass the ray's origin and direction to the payload
    payload.surf_id = ~0u;
    payload.tlas = record.world;

    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 2, 0, rayDesc, payload);

    // Store the result of the point in volume check and the surface ID of the primitive hit
    // 0 = outside, 1 = inside
    record.out[rayID].normal = payload.normal; 
    record.out.surf_id = payload.surf_id;
}

// ------------------------------------------------- Compute Shaders -------------------------------------------------
/* A shader to compute and store AABB min/maxes in single precision using double precision coords*/
[shader("compute")]
[numthreads(1, 1, 1)]
void
populate_aabbs(uint3 DispatchThreadID: SV_DispatchThreadID, uniform DPTriangleGeomData record) {
    int primID = DispatchThreadID.x;
    int3 indices = record.index[primID];
    double3 A = record.vertex[indices.x];
    double3 B = record.vertex[indices.y];
    double3 C = record.vertex[indices.z];
    double3 dpaabbmin = min(min(A, B), C);
    double3 dpaabbmax = max(max(A, B), C);
    float3 fpaabbmin = float3(dpaabbmin - float3(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON));
    float3 fpaabbmax = float3(dpaabbmax + float3(FLT_EPSILON, FLT_EPSILON, FLT_EPSILON));
    // printf("AABB %i: min(%f, %f, %f), max(%f, %f, %f)\n", primID, fpaabbmin.x, fpaabbmin.y, fpaabbmin.z,
    //        fpaabbmax.x, fpaabbmax.y, fpaabbmax.z);
    record.aabbs[2 * primID] = fpaabbmin;
    record.aabbs[2 * primID + 1] = fpaabbmax;
}

// ------------------------------------------------ CUSTOM INTERSECTION SHADERS ------------------------------------------------


/* 1D ray generation intersection with a double precision triangle using the Plucker intersection algorithm*/
[shader("intersection")]
void DPTrianglePluckerIntersection(uniform DPTriangleGeomData record, uniform dblRayGenData rayGenData)
{
    uint rayID = DispatchRaysIndex().x;
    uint nRays = DispatchRaysDimensions().x;

    if (rayID >= nRays) {
        printf("Rejected: rayID %u >= nRays %u\n", rayID, nRays);
        return;
    }

    uint flags = RayFlags();

    if (((flags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0) &&
        ((flags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0)) {
        printf("Rejected: both front and back facing triangles are culled\n");
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




    int primID = PrimitiveIndex();
    int3 indices = record.index[primID];
    double3 v0 = record.vertex[indices.x];
    double3 v1 = record.vertex[indices.y];
    double3 v2 = record.vertex[indices.z];

    double3 origin = record.ray[rayID].origin;
    double3 direction = record.ray[rayID].direction;

    float tMin = 0.0;
    float tMax = 10000;

    const double3 raya = direction;
    const double3 rayb = dp_cross(v1 - v0, v2 - v0); 

    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);
    if (useOrientation && orientation * plucker_coord0 > 0) {
        printf("Rejected: orientation check failed at edge 0 (plucker_coord0 = %f)\n", plucker_coord0);
        return;
    }

    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);
    if (useOrientation && orientation * plucker_coord1 > 0) {
        printf("Rejected: orientation check failed at edge 1 (plucker_coord1 = %f)\n", plucker_coord1);
        return;
    }
    else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
        printf("Rejected: winding check failed between edge 0 and edge 1\n");
        return;
    }

    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);
    printf("Plücker coords: %f, %f, %f\n", plucker_coord0, plucker_coord1, plucker_coord2);
    if (useOrientation && orientation * plucker_coord2 > 0) {
        printf("Rejected: orientation check failed at edge 2 (plucker_coord2 = %f)\n", plucker_coord2);
        return;
    }
    else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
        printf("Rejected: winding check failed across multiple edges\n");
        return;
    }

    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
        printf("Rejected: coplanar triangle (all Plücker coords are zero)\n");
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
        printf("Rejected: barycentric coordinates out of bounds (u = %f, v = %f)\n", u, v);
        t = -1.0;
    }
    if (t > tMax) {
        printf("Rejected: t = %f exceeds tMax = %f\n", t, tMax);
        return;
    }
    if (t < tMin) {
        printf("Rejected: t = %f below tMin = %f\n", t, tMin);
        return;
    }

    DPAttribute attr;
    attr.hitDistance = t;

    float f32t = float(t);
    if (double(f32t) < t) f32t = next_after(f32t);

    double3 norm = dcross(double3(v1 - v0), double3(v2 - v0));
    // sense check 
    if (record.sense == 1) {
        norm = -norm; // reverse the normal if the triangle is reverse sense
        printf("Reverse the normal \n");
    }

    if (orientation_cull(direction, norm, orientation)) 
    {
        printf("Rejected: Orientation does not match sense \n ");
        return;
    }

    if (record.ray[rayID].exclude_count == 1) 
    {
        printf("primitive id: %d, exlclude_prim: %d \n", record.prim_ids[primID], record.ray[rayID].exclude_primitives[0]);
    }
        for (int i = 0; i < record.ray[rayID].exclude_count; ++i)
    {
        if (record.ray[rayID].exclude_primitives[i] == primID) {
            printf("Rejected: primitive %d is in the exclude list\n", primID);
            return;
        }
    }

    uint hit_kind = dot(norm, direction) < 0 ? HIT_KIND_TRIANGLE_FRONT_FACE
                                             : HIT_KIND_TRIANGLE_BACK_FACE;

    ReportHit(f32t, hit_kind, attr);
}


// ------------------------------------------------- Helper functions -------------------------------------------------

bool orientation_cull(in double3 ray, in double3 normal, in int orientation) {
    if (orientation == -1) return false; // No culling
    if (orientation == 0) return dot(ray, normal) < 0.0; // Cull exiting rays
    if (orientation == 1) return dot(ray, normal) > 0.0; // Cull entering rays
    return false; // Default case, no culling
}


// Double precision cross product
double3 dp_cross(in double3 a, in double3 b) { return double3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// Plucker coordinate
double plucker_edge_test(in double3 vertexa, in double3 vertexb, in double3 ray, in double3 ray_normal)
{
    double pip;
    const double near_zero = 10 * DBL_EPSILON;

    if (first(vertexa, vertexb))
    {
        double3 edge = vertexb - vertexa;
        double3 edge_normal = dp_cross(edge, vertexa);
        pip = dot(ray, edge_normal) + dot(ray_normal, edge);
    }
    else
    {
        double3 edge = vertexa - vertexb;
        double3 edge_normal = dp_cross(edge, vertexb);
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
    if (a.x < b.x) return true;

    if (a.x == b.x && a.y < b.y) return true;

    if (a.y == b.y && a.z < b.z) return true;

    return false;
}

double3 dcross (in double3 a, in double3 b) { return double3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }

float next_after(float a) {
  uint a_ = asuint(a);
  if (a < 0) {
    a_--;
  } else {
    a_++;
  }
  return asfloat(a_);
}