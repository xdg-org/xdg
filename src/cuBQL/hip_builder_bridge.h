#ifndef XDG_CUBQL_HIP_BUILDER_BRIDGE_H
#define XDG_CUBQL_HIP_BUILDER_BRIDGE_H

#include "host_bvh.h"

namespace xdg::cubql {

HostBVH build_hip_bvh(const std::vector<cuBQL::box3f>& boxes,
                      cuBQL::BuildConfig config,
                      int device);

} // namespace xdg::cubql

#endif // XDG_CUBQL_HIP_BUILDER_BRIDGE_H
