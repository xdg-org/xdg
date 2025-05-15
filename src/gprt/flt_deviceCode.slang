#include "sharedCode.h"

[[vk::push_constant]]
PushConstants pc;

[[vk::push_constant]]
RayFirePushConstants rayFirePC;
struct Payload {
    float3 color;
};

struct SinglePayload {
    float distance;
    uint surfaceID;
};


// Shaders for single precision 1DRayGen

[shader("closesthit")]
void ray_fire_hit(uniform TrianglesGeomData record, inout SinglePayload payload, in float2 bc) {
    // Distance from the ray origin to the hit point
    payload.distance = RayTCurrent();
    payload.surfaceID = record.id;
    printf("Closest Hit called! Distance: %f, SurfaceID: %u\n", payload.distance, payload.surfaceID);
}

[shader("miss")]
void ray_fire_miss(inout SinglePayload payload) {
    // Set the miss payload to default values
    payload.distance = -1.0f;
    payload.surfaceID = ~0u;
    printf("Miss shader called!\n");
}

// This ray generation program will kick off the ray tracing process,
// generating rays and tracing them into the world.
[shader("raygeneration")]
void ray_fire(uniform RayGenData record) {
    SinglePayload payload;
    uint rayID = DispatchRaysIndex().x;
    printf("Ray Generation Shader called with :");
    printf("Ray Origin: (%f, %f, %f)\n", record.ray.origin.x, record.ray.origin.y, record.ray.origin.z);
    printf("Ray Direction: (%f, %f, %f)\n", record.ray.direction.x, record.ray.direction.y, record.ray.direction.z);
    
    // Trace the ray into the scene
    RayDesc rayDesc;
    rayDesc.Origin = record.ray.origin;
    rayDesc.Direction = record.ray.direction;
    rayDesc.TMin = 0.0;
    rayDesc.TMax = 10000.0;
    payload.distance = -1.0f;
    payload.surfaceID = ~0u;
    TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 1, 0, rayDesc, payload);

    // // Store the distance to the hit point and the surface ID in buffers for CPU
    // record.rayOutputs[rayID].distance = payload.distance;
    // record.rayOutputs[rayID].surfaceID = payload.surfaceID;
    record.out.distance = payload.distance;
    record.out.surfaceID = payload.surfaceID;
}
