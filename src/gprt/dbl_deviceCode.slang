#include "sharedCode.h"

[[vk::push_constant]]
dblRayFirePushConstants PC;

struct RayFirePayload {
    double distance;    // Distance to intersection
    int surf_id;     // ID of the surface hit
    int2 vol_ids;       // ID of the volume the ray is in
    SurfaceAccelerationStructure tlas;
    int next_vol;      // ID of the next volume
    uint primitive_id; // ID of the primitive hit
    uint piv; // Point in volume check (0 for outside, 1 for inside) 
};

struct DPAttribute
{
  double f64t; // double precision hit distance
  uint global_prim_id;
};


[shader("closesthit")]
void ray_fire_hit(uniform DPTriangleGeomData record, inout RayFirePayload payload, in DPAttribute attr) {
    // Distance from the ray origin to the hit point
    uint hit_kind = HitKind();
    uint rayID = DispatchRaysIndex().x;

    // The logic for next_volume isn't actually used just yet but I have copied it over from the original h5m reader code
    // TODO : Should the rayOutput struct return the next volume ID for the ray back to the host

    if (hit_kind == HIT_KIND_TRIANGLE_FRONT_FACE){
        payload.vol_ids.x = record.vols[0];
        payload.vol_ids.y = record.vols[1];
        payload.next_vol = record.reverse_vol;
        payload.piv = 0; // front face hit implies point outside the volume
    }
    else {
        payload.vol_ids.x = record.vols[1];
        payload.vol_ids.y = record.vols[0];
        payload.next_vol = record.forward_vol;
        payload.piv = 1; // back face hit implies point inside the volume
    }
    int instanceID = InstanceID();

    payload.distance = attr.f64t;
    payload.surf_id = record.surf_id;
    payload.primitive_id = attr.global_prim_id;
}

[shader("miss")]
void ray_fire_miss(inout RayFirePayload payload) {
    // Set the miss payload to default values
    payload.distance = -1.0f;
    payload.surf_id = ~0u;
    payload.primitive_id = ~0u;

    // printf("Miss shader called, setting default payload values\n");
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

    // Pass the ray's origin and direction to the payload
    payload.distance = -1.0f;
    payload.surf_id = ~0u;
    payload.tlas = record.world;

    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 1, 0, rayDesc, payload);

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

    // Pass the ray's origin and direction to the payload
    payload.surf_id = ~0u;
    payload.tlas = record.world;
    payload.piv = 0; // Initialize point in volume check result to outside (0)

    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 1, 0, rayDesc, payload);

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
    // // printf("\nNew Intersection found\n--------------------------------------------------------");
    uint rayID = DispatchRaysIndex().x;
    uint nRays = DispatchRaysDimensions().x;
    uint flags = RayFlags();

    if (rayID >= nRays) {
        // // printf("Rejected Intersection at line %u due to:  rayID %u >= nRays %u\n", __LINE__, rayID, nRays);
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

    double3 origin = record.rayIn[rayID].origin;
    double3 direction = record.rayIn[rayID].direction;

    // double tMin = record.rayIn[rayID].tMin;
    double tMin = record.rayIn[rayID].tMin;
    double tMax = record.rayIn[rayID].tMax;

    const double3 raya = direction;
    const double3 rayb = dp_cross(direction, origin);

    double plucker_coord0 = plucker_edge_test(v0, v1, raya, rayb);
    if (useOrientation && orientation * plucker_coord0 > 0) {
        // printf("Rejected Intersection at line %u due to:  orientation check failed at edge 0 (plucker_coord0 = %f)\n", __LINE__, plucker_coord0);
        return;
    }

    double plucker_coord1 = plucker_edge_test(v1, v2, raya, rayb);
    if (useOrientation && orientation * plucker_coord1 > 0) {
        // printf("Rejected Intersection at line %u due to:  orientation check failed at edge 1 (plucker_coord1 = %f)\n", __LINE__, plucker_coord1);
        return;
    }
    else if ((0.0 < plucker_coord0 && 0.0 > plucker_coord1) || (0.0 > plucker_coord0 && 0.0 < plucker_coord1)) {
        // printf("Rejected Intersection at line %u due to:  winding check failed between edge 0 and edge 1\n", __LINE__);
        return;
    }

    double plucker_coord2 = plucker_edge_test(v2, v0, raya, rayb);
    if (useOrientation && orientation * plucker_coord2 > 0) {
        // printf("Rejected Intersection at line %u due to:  orientation check failed at edge 2 (plucker_coord2 = %f)\n", __LINE__, plucker_coord2);
        return;
    }
    else if ((0.0 < plucker_coord1 && 0.0 > plucker_coord2) || (0.0 > plucker_coord1 && 0.0 < plucker_coord2) ||
             (0.0 < plucker_coord0 && 0.0 > plucker_coord2) || (0.0 > plucker_coord0 && 0.0 < plucker_coord2)) {
        // printf("Rejected Intersection at line %u due to:  winding check failed across multiple edges\n", __LINE__);
        return;
    }

    if (0.0 == plucker_coord0 && 0.0 == plucker_coord1 && 0.0 == plucker_coord2) {
        // printf("Rejected Intersection at line %u due to:  coplanar triangle (all Plücker coords are zero)\n", __LINE__);
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
        // printf("Rejected Intersection at line %u due to:  barycentric coordinates out of bounds (u = %f, v = %f)\n", __LINE__, u, v);
        t = -1.0;
    }
    if (t > tMax) {
        // printf("Rejected Intersection at line %u due to:  t = %f exceeds tMax = %f\n", __LINE__, t, tMax);
        return;
    }
    if (t < tMin) {
        // printf("Rejected Intersection at line %u due to:  t = %f below tMin = %f\n", __LINE__, t, tMin);
        return;
    }

    DPAttribute attr;
    attr.f64t = t;

    float f32t = float(t);
    if (double(f32t) < t) f32t = next_after(f32t);
    int global_prim_id = record.primitive_refs[primID].id;

    double3 norm = record.normals[primID];

    // sense adjustment of normal
    if (record.primitive_refs[primID].sense == 1)
    {
        // printf("Reversing the normal due to reverse sense\n");
        norm = -norm; // reverse the normal if the triangle is in the reverse volume
    }

    double norm_dot_dir = dot(norm, direction);
    uint hit_kind = norm_dot_dir < 0 ? HIT_KIND_TRIANGLE_FRONT_FACE
                                     : HIT_KIND_TRIANGLE_BACK_FACE;

    int hitOrientation = record.rayIn[rayID].hitOrientation;

    if (orientation_cull(direction, norm, hitOrientation))
    {
        // printf("Rejected Intersection at line %u due to:  Orientation does not match sense \n", __LINE__);
        return;
    }

    for (int i = 0; i < record.rayIn[rayID].exclude_count; ++i)
    {
        if (record.rayIn[rayID].exclude_primitives[i] == global_prim_id) {
            // printf("Rejected Intersection at line %u due to:  primitive %d is in the exclude list\n", __LINE__, global_prim_id);
            return;
        }
    }
    attr.global_prim_id = global_prim_id;  

    printf("Hit primitive %d at distance %f with normal (%f, %f, %f) and sense %d\n",
           record.primitive_refs[primID].id, t, norm.x, norm.y, norm.z, record.primitive_refs[primID].sense);
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

    // // // printf("plucker_edge_test: vertexa = (%f, %f, %f), vertexb = (%f, %f, %f), ray = (%f, %f, %f), ray_normal = (%f, %f, %f), pip = %f\n",
        //    vertexa.x, vertexa.y, vertexa.z,
        //    vertexb.x, vertexb.y, vertexb.z,
        //    ray.x, ray.y, ray.z,
        //    ray_normal.x, ray_normal.y, ray_normal.z,
        //    pip);

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