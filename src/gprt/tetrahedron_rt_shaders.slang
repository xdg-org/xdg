#ifndef XDG_GPRT_TETRAHEDRON_RT_SHADERS_SLANG
#define XDG_GPRT_TETRAHEDRON_RT_SHADERS_SLANG

#include "../../include/xdg/gprt/rt_common.slangh"

struct SolidRayFirePayload {
    int primitive_id; // ID of the primitive the point is contained within
};


// ------------------------------------------------- Compute Shaders -------------------------------------------------
/* A shader to compute and store AABB min/maxes in single precision using double precision coords for Tetrahedra*/
[shader("compute")]
[numthreads(1, 1, 1)]
void
populate_tet_aabbs(uint3 DispatchThreadID: SV_DispatchThreadID, uniform DPTetrahedronGeomData record) {
    uint primID = DispatchThreadID.x;
    uint4 indices = record.index[primID];
    uint vertexIndices[4] = { indices.x, indices.y, indices.z, indices.w };
    populate_aabb<4>(primID, record.vertex, record.aabbs, vertexIndices);
}

[shader("raygeneration")]
void find_element(uniform dblRayGenData record) {
    // This shader is only used to find the element containing a point, so we can ignore the raygen data and just use the ray's origin as the point to test for containment
    SolidRayFirePayload payload;
    uint rayID = DispatchRaysIndex().x;
    PointDesc pointDesc;
    pointDesc.Origin = float3(record.ray[rayID].origin);

    SolidAccelerationStructure world = record.ray[rayID].volume_accel_solid;

    // Pass the ray's origin and direction to the payload
    payload.primitive_id = ID_NONE; // Initialize to -1 to indicate the point is

    TracePoint(world, RAY_FLAG_NONE, 0xff, xdg::RT_VOLUME_RAY_INDEX, xdg::RT_VOLUME_MISS_INDEX, pointDesc, payload);

    // Store the ID of the containing element in the hit buffer for CPU readback
    record.hit[rayID].primitive_id = payload.primitive_id; // we're reusing dblHit's primitive_id field to store the ID of the containing element since we don't need hit distance or surf_id for this shader
}

[shader("miss")]
void tet_miss(inout SolidRayFirePayload payload) {
    payload.primitive_id = ID_NONE; // Mark the ray as outside all tetrahedra
}

[shader("closesthit")]
void tet_contain_hit(uniform DPTetrahedronGeomData record, inout SolidRayFirePayload payload, in DPAttribute attr) {
    payload.primitive_id = attr.global_prim_id; // Store the ID of the tet that contains the point
}


/* 1D ray generation intersection with a double precision Tetrahedron using the Plucker intersection algorithm*/
[shader("intersection")]
void DPTetrahedronPluckerIntersection(uniform DPTetrahedronGeomData record)
{
    int primID = PrimitiveIndex();
    int global_prim_id = record.primitive_refs[primID].id;

    uint rayID = DispatchRaysIndex().x;
    uint nRays = DispatchRaysDimensions().x;
    uint flags = RayFlags();

    if (rayID >= nRays) {
        return;
    }

    int4 indices = record.index[primID];
    double3 v0 = record.vertex[indices[0]];
    double3 v1 = record.vertex[indices[1]];
    double3 v2 = record.vertex[indices[2]];
    double3 v3 = record.vertex[indices[3]];
    double3 origin = record.ray[rayID].origin;

    bool inside = xdg::plucker_tet_containment_test(origin, v0, v1, v2, v3);
    if (!inside) return;

    // If we are inside the tet we need to report a hit to move through the rest of the RT pipeline
    float f32t = 0.0f; // We dont care about intersection distance
    uint hit_kind = 0; // No hit kind since it doesn't matter
    
    DPAttribute attr;
    attr.global_prim_id = global_prim_id;
    ReportHit(f32t, hit_kind, attr);
}

#endif // XDG_GPRT_TETRAHEDRON_RT_SHADERS_SLANG
