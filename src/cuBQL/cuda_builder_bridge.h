#ifndef XDG_CUBQL_CUDA_BUILDER_BRIDGE_H
#define XDG_CUBQL_CUDA_BUILDER_BRIDGE_H

#include <cstdint>
#include <vector>

#include "cuBQL/bvh.h"

namespace xdg::cubql {

struct HostBVH {
  std::vector<cuBQL::bvh3f::node_t> nodes;
  std::vector<std::uint32_t> prim_ids;
};

HostBVH build_cuda_bvh(const std::vector<cuBQL::box3f>& boxes,
                       cuBQL::BuildConfig config,
                       int device);

} // namespace xdg::cubql

#endif // XDG_CUBQL_CUDA_BUILDER_BRIDGE_H
