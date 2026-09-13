#define CUBQL_GPU_BUILDER_IMPLEMENTATION 1
#include "hip_builder_bridge.h"

#include <hip/hip_runtime.h>

// cuBQL shares its GPU builder between CUDA and HIP; compiling this file as
// HIP selects the HIP implementation through __HIPCC__.
#include "cuBQL/builder/cuda.h"

namespace xdg::cubql {
HostBVH build_hip_bvh(const std::vector<cuBQL::box3f>& boxes,
                      cuBQL::BuildConfig config,
                      int device)
{
  hipSetDevice(device);

  cuBQL::box3f* device_boxes = nullptr;
  hipMalloc(reinterpret_cast<void**>(&device_boxes),
            boxes.size() * sizeof(cuBQL::box3f));
  hipMemcpy(device_boxes,
            boxes.data(),
            boxes.size() * sizeof(cuBQL::box3f),
            hipMemcpyHostToDevice);

  cuBQL::bvh3f hip_bvh;
  cuBQL::DeviceMemoryResource memory_resource;
  cuBQL::gpuBuilder(hip_bvh,
                    device_boxes,
                    static_cast<std::uint32_t>(boxes.size()),
                    config,
                    0,
                    memory_resource);
  hipDeviceSynchronize();

  HostBVH host_bvh;
  host_bvh.nodes.resize(hip_bvh.numNodes);
  host_bvh.prim_ids.resize(hip_bvh.numPrims);
  hipMemcpy(host_bvh.nodes.data(),
            hip_bvh.nodes,
            host_bvh.nodes.size() * sizeof(cuBQL::bvh3f::node_t),
            hipMemcpyDeviceToHost);
  hipMemcpy(host_bvh.prim_ids.data(),
            hip_bvh.primIDs,
            host_bvh.prim_ids.size() * sizeof(std::uint32_t),
            hipMemcpyDeviceToHost);

  cuBQL::cuda::free(hip_bvh, 0, memory_resource);
  hipFree(device_boxes);
  return host_bvh;
}

} // namespace xdg::cubql
