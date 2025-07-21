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
};

struct DPAttribute
{
  float2 bc; // barycentrics
};


[shader("closesthit")]
void ray_fire_hit(uniform DPTriangleGeomData record, inout RayFirePayload payload, in float2 bc) {
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
    payload.distance = record.hitDistance;
    payload.surf_id = record.surf_id;
    // payload.normal = record.normals[PrimitiveIndex()];
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

    // printf("Ray Generation Shader called with :");
    // printf("Ray Origin: (%f, %f, %f)\n", record.ray.origin.x, record.ray.origin.y, record.ray.origin.z);
    // printf("Ray Direction: (%f, %f, %f)\n", record.ray.direction.x, record.ray.direction.y, record.ray.direction.z);
    // printf("Excluded Primitives.size(): %i \n", record.ray.exclude_count);

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
    TraceRay(record.world, RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, 0xff, 0, 1, 0, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.out[rayID].distance = payload.distance;
    record.out[rayID].surf_id = payload.surf_id;
}

[shader("raygeneration")]
void point_in_volume(uniform RayGenData record, uniform TrianglesGeomData mesh) {
    RayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;

    // printf("Ray Generation Shader called with :");
    // printf("Ray Origin: (%f, %f, %f)\n", record.ray.origin.x, record.ray.origin.y, record.ray.origin.z);
    // printf("Ray Direction: (%f, %f, %f)\n", record.ray.direction.x, record.ray.direction.y, record.ray.direction.z);
    // printf("Excluded Primitives.size(): %i \n", record.ray.exclude_count);

    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = record.ray.origin;
    rayDesc.Direction = normalize(record.ray.direction);
    rayDesc.TMin = 1e-4f; // non-zero to avoid self-intersection
    rayDesc.TMax = 1e30f;

    // Pass the ray's origin and direction to the payload
    payload.surf_id = ~0u;
    payload.tlas = record.world;

    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 2, 0, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
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
    printf("DPTrianglePluckerIntersection called with rayID: %u, nRays: %u\n", rayID, nRays);

    if (rayID >= nRays) {
        printf("Early return at line %d: rayID (%u) >= nRays (%u)\n", __LINE__, rayID, nRays);
        return;
    }

    uint flags = RayFlags();

    if (((flags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0) &&
        ((flags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0)) {
        printf("Early return at line %d: Both cull flags set\n", __LINE__);
        return;
    }

    // Set the ray orientation based on the flags
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

    // Recover our double precision rays from the stored buffer
    // double4 raydata1 = record.dprays[rayID * 2 + 0];
    // double4 raydata2 = record.dprays[rayID * 2 + 1];
    // double3 origin = double3(raydata1.x, raydata1.y, raydata1.z);
    // double3 direction = double3(raydata2.x, raydata2.y, raydata2.z);
    // double tMin = raydata1.w;
    // double tMax = raydata2.w;

    double3 origin = record.ray[rayID].origin;
    double3 direction = record.ray[rayID].direction;

    // double tMin = rayGenData.ray[rayID].tMin;
    // double tMax = rayGenData.ray[rayID].tMax;

    // hard coded for now. Need to be passed from host->raygen->intersection shader
    float tMin = 0.0;
    float tMax = 10000;

    printf("tmin: %f, tCurrent: %f\n", tMin, tMax);


    const double3 raya = direction;
    const double3 rayb = dp_cross(v1 - v0, v2 - v0);

    // Determine the value of first Plucker coordinate from edge 0
    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord0 > 0) {
        printf("Early return at line %d: orientation * plucker_coord0 > 0\n", __LINE__);
        return;
    }

    // Determine the value of the second Plucker coordinate from edge 1
    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord1 > 0) {
        printf("Early return at line %d: orientation * plucker_coord1 > 0\n", __LINE__);
        return;
    }
    else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
        printf("Early return at line %d: plucker_coord0 and plucker_coord1 have opposite signs\n", __LINE__);
        return;
    }

    // Determine the value of the third Plucker coordinate from edge 2
    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord2 > 0) {
        printf("Early return at line %d: orientation * plucker_coord2 > 0\n", __LINE__);
        return;
    }
    else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
        printf("Early return at line %d: plucker_coords have opposite signs\n", __LINE__);
        return;
    }

    // check for coplanar case to avoid dividing by zero
    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
        printf("Early return at line %d: coplanar case, all plucker_coords are zero\n", __LINE__);
        return;
    }

    // Get the distance to the intersection point
    const double inverse_sum = 1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
    const double3 intersection = double3(plucker_coord0 * inverse_sum * v2 +
                                         plucker_coord1 * inverse_sum * v0 +
                                         plucker_coord2 * inverse_sum * v1);

    // To minimise numerical error, get index of largest magnitude direction component
    int idx = 0;
    double max_abs_dir = 0;
    for(uint i = 0; i < 3; ++i)
    {
        if( abs( direction[i] ) > max_abs_dir )
        {
            idx         = i;
            max_abs_dir = abs( direction[i] );
        }
    }
    const double dist = ( intersection[idx] - origin[idx] ) / direction[idx];

    double t = dist;
    double u = plucker_coord2 * inverse_sum;
    double v = plucker_coord0 * inverse_sum;

    if( u < 0.0 || v < 0.0 || (u+v) > 1.0 ) t = -1.0;
    if (t > tMax) return;
    if (t < tMin) return;

    // printf("Distance: %f", t);

    // update current double precision thit
    record.hitDistance = t;
    // rayGenData.out[rayID].distance = t;

    DPAttribute attr;
    attr.bc = float2(float(u), float(v));
    // attr.hitDistance = t;
    // printf("t: %f", (float)t);
    // printf("atrr.hitDistance: %f, attr.bc: (%f, %f)\n", attr.hitDistance, attr.bc.x, attr.bc.y);
    float f32t = float(t);
    if (double(f32t) < t) f32t = next_after(f32t);

    // // // // compute the triangle normal
    double3 norm = dcross(double3(v1 - v0), double3(v2 - v0));

    uint hit_kind = dot(norm, direction) < 0 ? HIT_KIND_TRIANGLE_FRONT_FACE
                                             : HIT_KIND_TRIANGLE_BACK_FACE;

    ReportHit(f32t, hit_kind, attr);
}

// ------------------------------------------------- Helper functions -------------------------------------------------

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