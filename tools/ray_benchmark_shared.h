#include "gprt.h"

#include "../include/xdg/gprt/ray.h"

struct GenerateRandomRayParams {
    xdg::dblRay* rays; // pointer to ray data buffer
    uint numRays; // number of rays to be generated
    double3 origin; // single origin provided for benchmark case
    uint seed; // seed for random direction generation
    uint total_threads;
};