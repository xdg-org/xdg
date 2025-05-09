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

// ------------------------------------------------ CUSTOM INTERSECTION SHADERS ------------------------------------------------
/* 1D ray generation intersection with a double precision triangle using the Plucker intersection algorithm*/
[shader("intersection")]
void DPTrianglePluckerIntersection(uniform DPTriangleGeomData record)
{
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