#include "test_direct_ray_buffer_access_shared.h"

[shader("compute")]
[numthreads(256, 1, 1)]
void pack_external_rays(uint3 DispatchThreadID: SV_DispatchThreadID,
                        uniform ExternalRayParams extParams)
{
    uint globalThreadID = DispatchThreadID.x;
    uint stride = extParams.total_threads; // Groups * 256

    // Grid-stride loop: each thread handles ray idx, idx+stride, idx+2*stride, ...
    for (uint idx = globalThreadID; idx < extParams.num_rays; idx += stride)
    {
        xdg::dblRay r;
        r.origin = extParams.origins[idx];
        r.direction = extParams.directions[idx];
        r.exclude_primitives = nullptr;
        r.exclude_count = 0;
        r.volume_mesh_id = extParams.volume_mesh_ids[idx]; // Set volume mesh ID per ray
        r.enabled = extParams.enabled;

        extParams.xdgRays[idx] = r; // Write to device ray buffer
    }
}
