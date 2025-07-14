#include "sharedCode.h"

[[vk::push_constant]]
PushConstants pc;

[[vk::push_constant]]
RayFirePushConstants rayFirePC;

struct Payload {
    float3 color;
};

struct RayFirePayload {
    float distance;    // Distance to intersection
    int surf_id;     // ID of the surface hit
    int vol_id;       // ID of the volume the ray is in 
    SurfaceAccelerationStructure tlas;
    int next_vol;      // ID of the next volume
    float3 normal;
};

[shader("closesthit")]
void ray_fire_hit(uniform TrianglesGeomData record, inout RayFirePayload payload, in float2 bc) {
    // Distance from the ray origin to the hit point
    uint hit_kind = HitKind();

    // if (hit_kind == HIT_KIND_TRIANGLE_FRONT_FACE) {
    //   payload.vol_ids.x = record.vols[0]; // moving out of this volume
    //   payload.vol_ids.y = record.vols[1]; // moving into this volume
    //   payload.next_vol = record.bf_vol; // moving into the backface volume
    // }
    // else {
    //   payload.vol_ids.x = record.vols[1]; // moving out of this volume
    //   payload.vol_ids.y = record.vols[0]; // moving into this volume
    //   payload.next_vol = record.ff_vol; // moving into the frontface volume
    //   uint2 pixelID = DispatchRaysIndex().xy;
    //   uint2 centerID = DispatchRaysDimensions().xy / 2;
    //   if (all(pixelID == centerID)) {
    //     // printf("Index of next volume BLAS %i", payload.next_vol);
    //     // printf("Going from volume %i into volume %i, ", payload.vol_ids.x, payload.vol_ids.y);
    //     printf("\nSurface ID: %i \n"
    //     "Geom Data Vols: {%i, %i}\n"
    //     "Index of next volume BLAS %i\n", record.id, record.vols[1], record.vols[0], record.ff_vol);
    //   }
    // }
 

    payload.distance = RayTCurrent();
    payload.surf_id = record.id;
    payload.normal = record.normals[PrimitiveIndex()];

    // Print the ray's origin and direction
    // printf("Closest Hit called!");
    // printf("Normal returned: (%f, %f, %f)", payload.normal.x, payload.normal.y, payload.normal.z);
    // printf("Closest Hit called! Distance: %f, surf_id: %u\n", payload.distance, record.id);
    // printf("Ray Origin: (%f, %f, %f)\n", payload.origin.x, payload.origin.y, payload.origin.z);
    // printf("Ray Direction: (%f, %f, %f)\n", payload.direction.x, payload.direction.y, payload.direction.z);
    // printf("Barycentrics: (%f, %f)\n", bc.x, bc.y);
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
void ray_fire(uniform RayGenData record, uniform TrianglesGeomData mesh) {
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
    rayDesc.TMax = rayFirePC.dist_limit; // distance limit from push constants
    

    // Pass the ray's origin and direction to the payload
    payload.distance = -1.0f;
    payload.surf_id = ~0u;   
    payload.tlas = record.world;


    uint rayFlagCull = (rayFirePC.orientation == 1) 
                   ? RAY_FLAG_CULL_FRONT_FACING_TRIANGLES 
                   : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    

    // printf("Distance Limit: %f\n", rayFirePC.dist_limit);
    // printf("Orientation: %u\n", rayFirePC.orientation);
    // printf("TMax before TraceRay: %f\n PC.dist_limit: %f\n", rayDesc.TMax, rayFirePC.dist_limit);
    // printf("Exclude_primitive size: %i \n", record.ray.exclude_count);
    TraceRay(record.world, rayFlagCull, 0xff, 0, 1, 0, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.out.distance = payload.distance;
    record.out.surf_id = payload.surf_id;
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
    record.out.normal = payload.normal;
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
    record.aabbs[2 * primID] = fpaabbmin;
    record.aabbs[2 * primID + 1] = fpaabbmax;
}

// ------------------------------------------------ CUSTOM INTERSECTION SHADERS ------------------------------------------------
/* 1D ray generation intersection with a double precision triangle using the Plucker intersection algorithm*/
[shader("intersection")]
void DPTrianglePluckerIntersection(uniform DPTriangleGeomData record)
{

    printf("DPTrianglePluckerIntersection called!\n");
    uint rayID = DispatchRaysIndex().x;
    uint nRays = DispatchRaysDimensions().x;

    if (rayID >= nRays) return; // Just in case we are out of bounds

    // if ((pixelID.x == dims.x / 2) && (pixelID.y == dims.y / 2)) debug = true; // Debugging ray at center of screen
    // if ((rayID == nRays / 2)) debug = true; // Debugging ray at the center of array

    uint flags = RayFlags();

    // Just skip if we for some reason cull both...
    if (((flags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0) &&
        ((flags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0)) return;

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
    double4 raydata1 = record.dprays[rayID * 2 + 0];
    double4 raydata2 = record.dprays[rayID * 2 + 1];
    double3 origin = double3(raydata1.x, raydata1.y, raydata1.z);
    double3 direction = double3(raydata2.x, raydata2.y, raydata2.z);
    double tMin = raydata1.w;
    double tMax = raydata2.w;

    const double3 raya = direction;
    const double3 rayb = dp_cross(v1 - v0, v2 - v0);

    // Determine the value of first Plucker coordinate from edge 0
    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord0 > 0) {
        return;
    }

    // Determine the value of the second Plucker coordinate from edge 1
    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord1 > 0) return;
    // If the orientation is not specified, all plucker_coords must be the same sign or zero.
    else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) return;

    // Determine the value of the third Plucker coordinate from edge 2
    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);

    // If orientation is set, confirm that sign of plucker_coordinate indicate correct orientation of intersection
    if (useOrientation && orientation * plucker_coord2 > 0) return;
    // If the orientation is not specified, all plucker_coords must be the same sign or zero.
    else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2))
    {
        return; // EXIT_EARLY;
    }

    // check for coplanar case to avoid dividing by zero
    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
        return; // EXIT_EARLY;
    }

    // Get the distance to the intersection point
    const double inverse_sum = 1.0 / (plucker_coord0 + plucker_coord1 + plucker_coord2);
    const double3 intersection = double3(plucker_coord0 * inverse_sum * v2 +
                                         plucker_coord1 * inverse_sum * v0 +
                                         plucker_coord2 * inverse_sum * v1);

    // To minimise numerical error, get index of largest magnitude direction component
    int idx = 0;
    printf("We are here in the double precision intersection shader!\n");

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