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
    float3 origin;     // Add ray origin
    float3 direction;  // Add ray direction
};


// This closest hit shader runs when a ray hits a triangle.
// It processes per-geometry data and communicates with the ray generation shader.
// - The first parameter is the shader record type, representing SBT parameters for this shader.
// - The second is the ray payload type, used for passing data between shaders.
// - The third is the intersection attributes. For triangles, these are two "barycentrics", used
//   for interpolating per-vertex values.
[shader("closesthit")]
void TriangleMesh(uniform TrianglesGeomData record, inout Payload payload, in float2 bc) {
    // Threshold to determine edge thickness
    const float edgeThreshold = 0.02;

    // Check if the ray is near an edge using barycentric coordinates
    if (bc.x < edgeThreshold || bc.y < edgeThreshold || (1.0 - (bc.x + bc.y)) < edgeThreshold) {
        // Near an edge: set color to black
        payload.color = float3(0.0, 0.0, 0.0);
    } else { // Generate a unique color for each parent volume
        // uint vol = record.vols.x; // Get first parent volume ID
        // float r = float((vol * 37) % 256) / 255.0;  
        // float g = float((vol * 73) % 256) / 255.0;  
        // float b = float((vol * 151) % 256) / 255.0; 
        // payload.color = float3(r, g, b);
        payload.color = float3(0.0, 1.0, 0.0);
    }
}

// This ray generation program will kick off the ray tracing process,
// generating rays and tracing them into the world.
[shader("raygeneration")]
void raygen(uniform RayGenData record) {
  Payload payload;
  uint2 pixelID = DispatchRaysIndex().xy;
  uint2 iResolution = DispatchRaysDimensions().xy;

  // camera movement
  float an = pc.time;
  
  float zoom = 100; // Increase the radius over time
  float3 ro = float3(-zoom * sin(an), 0.0, zoom * cos(an));
  float3 ta = float3(0.0, 0.0, 0.0);

  // camera matrix
  float3 ww = normalize(ta - ro);
  float3 uu = normalize(cross(ww, float3(0.0, -1.0, 0.0)));
  float3 vv = normalize(cross(uu, ww));

  float3 tot = float3(0.0);

  for (int m = 0; m < AA; m++) {
  for (int n = 0; n < AA; n++) {
    // pixel coordinates
    float2 o = float2(float(m), float(n)) / float(AA) - 0.5;
    float2 p = (2.0 * (pixelID + o) - iResolution.xy) / iResolution.y;

    // create view ray
    float3 rd = normalize(p.x * uu + p.y * vv + 4.0 * ww);

    RayDesc rayDesc;
    rayDesc.Origin = ro;
    rayDesc.Direction = rd;
    rayDesc.TMin = 0.0;
    rayDesc.TMax = 10000.0;
    
    payload.color = float3(0.0, 0.0, 0.0); // Default color
    TraceRay(record.world,     // the tree
              RAY_FLAG_NONE,   // ray flags
              0xff,            // instance inclusion mask
              0,               // ray type
              1,               // number of ray types
              0,               // miss index
              rayDesc,         // the ray to trace
              payload          // the payload IO
    );

    tot += payload.color;
  }
  }

  tot /= float(AA * AA);

  const int fbOfs = pixelID.x + iResolution.x * pixelID.y;
  record.frameBuffer[fbOfs] = gprt::make_bgra(tot);
}

[shader("miss")]
void miss(inout Payload payload, inout RayDesc rayDesc) {
    // Set a gradient background based on the ray direction
    float3 dir = normalize(rayDesc.Direction);
    payload.color = float3(1.0, 1.0, 1.0);
}


// Shaders for single precision 1DRayGen

[shader("closesthit")]
void ray_fire_hit(uniform TrianglesGeomData record, inout SinglePayload payload, in float2 bc) {
    // Distance from the ray origin to the hit point
    payload.distance = RayTCurrent();
    payload.surfaceID = record.id;

    // Print the ray's origin and direction
    printf("Closest Hit called! Distance: %f, SurfaceID: %u\n", payload.distance, record.id);
    printf("Ray Origin: (%f, %f, %f)\n", payload.origin.x, payload.origin.y, payload.origin.z);
    printf("Ray Direction: (%f, %f, %f)\n", payload.direction.x, payload.direction.y, payload.direction.z);
    printf("Barycentrics: (%f, %f)\n", bc.x, bc.y);
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
    rayDesc.Direction = normalize(record.ray.direction);
    rayDesc.TMin = 1e-4f; // non-zero to avoid self-intersection
    rayDesc.TMax = 10000;//rayFirePC.dist_limit; // distance limit from push constants

    // Pass the ray's origin and direction to the payload
    payload.origin = rayDesc.Origin;
    payload.direction = rayDesc.Direction;
    payload.distance = -1.0f;
    payload.surfaceID = ~0u;   
    
    // Recover correct flag based on ray's HitOrientation
    uint rayFlag = (rayFirePC.orientation == 1) ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : // HitOrientation::EXITING
                   (rayFirePC.orientation == 2) ? RAY_FLAG_CULL_FRONT_FACING_TRIANGLES : // HitOrientation::ENTERING
                                                  RAY_FLAG_NONE; // HitOrientation::ANY

    printf("Ray Flag: %u\n", rayFlag);
    printf("Distance Limit: %f\n", rayFirePC.dist_limit);
    printf("Orientation: %u\n", rayFirePC.orientation);

    TraceRay(record.world, RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, 0xff, 0, 1, 0, rayDesc, payload);

    // Store the distance to the hit point and the surface ID in buffers for CPU
    record.out.distance = payload.distance;
    record.out.surfaceID = payload.surfaceID;
}
