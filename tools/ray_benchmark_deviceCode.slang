#include "ray_benchmark_shared.h"

/* 
For this simple benchmark case we are mocking what a downstream application would do in terms of populating
ray buffers. The idea is that the downstream application generates rays (origins + directions).
*/
[shader("compute")]
[numthreads(64, 1, 1)]
void generate_random_rays(uint3 DispatchThreadID: SV_DispatchThreadID, uniform GenerateRandomRayParams params) {
    uint rayID = DispatchThreadID.x;
    uint nRays = params.numRays;

    if (rayID >= nRays) {
        return;
    }

    uint state = params.seed ^ rayID; // same as cpu: state = seed ^ i
    double3 dir = random_unit_dir(state);

    params.rays.directions[rayID] = dir;
    params.rays.origins[rayID] = params.origin; 
}

// Helpers 

// Simple LCG random number generator
double rand01(inout uint state)
{
    state = state * 1664525u + 1013904223u;
    return double(state) * (1.0 / 4294967296.0);
}

// return random unit dir
double3 random_unit_dir(inout uint state)
{
    double x1, x2, s;
    do {
        x1 = rand01(state) * 2.0 - 1.0;
        x2 = rand01(state) * 2.0 - 1.0;
        s  = x1 * x1 + x2 * x2;
    } while (s <= 0.0 || s >= 1.0);

    double t = 2.0 * sqrt(1.0 - s);
    return double3(x1 * t, x2 * t, 1.0 - 2.0 * s);
}