#include "sharedCode.h"

[[vk::push_constant]]
PushConstants pc;

struct Payload {
    float3 color;
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
        uint vol = record.vols.x; // Get first parent volume ID
        float r = float((vol * 37) % 256) / 255.0;  
        float g = float((vol * 73) % 256) / 255.0;  
        float b = float((vol * 151) % 256) / 255.0; 
        payload.color = float3(r, g, b);
    }
}

// This ray generation program will kick off the ray tracing process,
// generating rays and tracing them into the world.
[shader("raygeneration")]
void raygen(uniform RayGenData record) {
    Payload payload;
    uint2 pixelID = DispatchRaysIndex().xy;
    uint2 iResolution = DispatchRaysDimensions().xy;

    // Use camera parameters from PushConstants
    float3 ro = pc.camera.pos; // Camera position
    float3 ww = normalize(pc.camera.dir_00); // Forward direction
    float3 uu = normalize(pc.camera.dir_du); // Horizontal direction
    float3 vv = normalize(pc.camera.dir_dv); // Vertical direction

    float3 tot = float3(0.0);

    for (int m = 0; m < AA; m++)
        for (int n = 0; n < AA; n++)
        {
            // Pixel coordinates
            float2 o = float2(float(m), float(n)) / float(AA) - 0.5;
            float2 p = (2.0 * (pixelID + o) - iResolution.xy) / iResolution.y;

            // Create view ray
            float3 rd = normalize(p.x * uu + p.y * vv + ww);

            // Trace the ray into the scene
            RayDesc rayDesc;
            rayDesc.Origin = ro;
            rayDesc.Direction = rd;
            rayDesc.TMin = 0.0;
            rayDesc.TMax = 10000.0;
            TraceRay(record.world, RAY_FLAG_NONE, 0xff, 0, 1, 0, rayDesc, payload);

            // Accumulate the color
            tot += payload.color;
        }
    tot /= float(AA * AA);

    const int fbOfs = pixelID.x + iResolution.x * pixelID.y;
    record.frameBuffer[fbOfs] = gprt::make_bgra(tot);
}

[shader("miss")]
void miss(inout Payload payload) {
    // Set the background color to light gray
    payload.color = float3(0.8, 0.8, 0.8);
}
