#include "gprt.h"

#include "../include/xdg/gprt/ray.h"

struct GenerateRandomRayParams {
    xdg::dblRay* rays; // pointer to ray data buffer
    uint numRays; // number of rays to be generated
    double3 origin; // single origin provided for benchmark case
    uint seed; // seed for random direction generation
    uint total_threads;
    double source_radius; // 0.0 = point volume, >0.0 = spherical cloud
};