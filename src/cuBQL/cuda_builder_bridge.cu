#define CUBQL_GPU_BUILDER_IMPLEMENTATION 1
#include "cuda_builder_bridge.h"

#include <cuda_runtime_api.h>

#include "cuBQL/builder/cuda.h"

namespace xdg::cubql {
HostBVH build_cuda_bvh(const std::vector<cuBQL::box3f>& boxes,
                       cuBQL::BuildConfig config,
                       int device)
{
  cudaSetDevice(device);

  cuBQL::box3f* device_boxes = nullptr;
  cudaMalloc(reinterpret_cast<void**>(&device_boxes),
             boxes.size() * sizeof(cuBQL::box3f));
  cudaMemcpy(device_boxes,
             boxes.data(),
             boxes.size() * sizeof(cuBQL::box3f),
             cudaMemcpyHostToDevice);

  cuBQL::bvh3f cuda_bvh;
  cuBQL::DeviceMemoryResource memory_resource;
  cuBQL::gpuBuilder(cuda_bvh,
                    device_boxes,
                    static_cast<std::uint32_t>(boxes.size()),
                    config,
                    0,
                    memory_resource);
  cudaDeviceSynchronize();

  HostBVH host_bvh;
  host_bvh.nodes.resize(cuda_bvh.numNodes);
  host_bvh.prim_ids.resize(cuda_bvh.numPrims);
  cudaMemcpy(host_bvh.nodes.data(),
             cuda_bvh.nodes,
             host_bvh.nodes.size() * sizeof(cuBQL::bvh3f::node_t),
             cudaMemcpyDeviceToHost);
  cudaMemcpy(host_bvh.prim_ids.data(),
             cuda_bvh.primIDs,
             host_bvh.prim_ids.size() * sizeof(std::uint32_t),
             cudaMemcpyDeviceToHost);

  cuBQL::cuda::free(cuda_bvh, 0, memory_resource);
  cudaFree(device_boxes);
  return host_bvh;
}

} // namespace xdg::cubql
