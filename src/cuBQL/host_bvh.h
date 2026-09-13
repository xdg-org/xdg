#ifndef XDG_CUBQL_HOST_BVH_H
#define XDG_CUBQL_HOST_BVH_H

#include <cstdint>
#include <vector>

#include "cuBQL/bvh.h"

namespace xdg::cubql {

struct HostBVH {
  std::vector<cuBQL::bvh3f::node_t> nodes;
  std::vector<std::uint32_t> prim_ids;
};

} // namespace xdg::cubql

#endif // XDG_CUBQL_HOST_BVH_H
