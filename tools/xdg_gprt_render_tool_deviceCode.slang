#include "xdg_gprt_render_tool_shared.h"

[[vk::push_constant]]
PushConstants pc;

struct Payload {
    float3 color;
};

float4
over(float4 a, float4 b) {
  float4 result;
  result.a = a.a + b.a * (1.f - a.a);
  if (result.a == 0.f)
    return a;   // avoid NaN
  result.rgb = (a.rgb * a.a + b.rgb * b.a * (1.f - a.a)) / result.a;
  return result;
}

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
    uint2 pixelID = DispatchRaysIndex().xy;
    uint2 fbSize  = DispatchRaysDimensions().xy;
    if (pixelID.x >= fbSize.x || pixelID.y >= fbSize.y) return;

    const float aa = float(AA);
    float3 accum = float3(0.0, 0.0, 0.0);

    for (int m = 0; m < AA; m++) {
        for (int n = 0; n < AA; n++) {
            // Subpixel center within the pixel
            float2 sub = (float2(m + 0.5, n + 0.5) / aa);

            // screen in [0,1]^2 at pixel center
            float2 screen = (float2(pixelID) + sub) / float2(fbSize);

            // build ray from push-constant camera
            RayDesc ray;
            ray.Origin    = pc.camera.pos;
            ray.Direction = normalize(pc.camera.dir_00
                                    + screen.x * pc.camera.dir_du
                                    + screen.y * pc.camera.dir_dv);
            ray.TMin = 0.001;
            ray.TMax = 10000.0;

            // payload per ray
            Payload p; 
            p.color = float3(0,0,0);

            // rayType=0, missIndex=0 (one ray type)
            TraceRay(record.world, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, ray, p);
            accum += p.color;
        }
    }

    // Average the samples and write linear RGBA
    float3 avg = accum / (aa * aa);
    const int fbOfs = pixelID.x + fbSize.x * pixelID.y;
    record.imageBuffer[fbOfs] = float4(saturate(avg), 1.0f);
}

[shader("miss")]
void miss(inout Payload payload) {
    // Set the background color to light gray
    payload.color = float3(0.8, 0.8, 0.8);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void
CompositeGui(uint3 DispatchThreadID: SV_DispatchThreadID, uniform CompositeGuiConstants pc) {
  int2 pixelID = DispatchThreadID.xy;
  const int fbOfs = pixelID.x + pc.fbSize.x * pixelID.y;
  float2 fragCoord = pixelID + float2(.5f, .5f);
  float2 uv = (fragCoord) / float2(pc.fbSize);

  // Load color of the rendered image
  float4 imageColor = pc.imageBuffer[fbOfs];

  // Sample the color from the GUI texture
  SamplerState sampler = gprt::getDefaultSampler();
  DescriptorHandle<Texture2D<float4>> guiTexture = pc.guiTexture;
  float4 guiColor = guiTexture.SampleGrad(sampler, uv, float2(0.f, 0.f), float2(0.f, 0.f));

  // Gamma correction
  guiColor.rgb = gprt::srgb_to_linear(guiColor.rgb);

  // Composite the GUI on top of the scene
  float4 pixelColor = over(guiColor, imageColor);
  pc.frameBuffer[fbOfs] = gprt::make_bgra(pixelColor);
}