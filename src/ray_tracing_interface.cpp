#include <algorithm>
#include "xdg/ray_tracing_interface.h"

// So far, most of the methods in RayTracer require quite specific implementations to their RT backend.
// register_volume() is an exception to this.
// I imagine the find_volume algorithm may have some common logic which can sit in here once its implemented.

namespace xdg {

  RayTracer::~RayTracer() {}

TreeID RayTracer::next_tree_id() const
{
  const auto& tree_ids = trees();
  if (tree_ids.empty()) {
    return 1; // start at 1 to reserve 0 for ipc?
  }
  return *std::max_element(tree_ids.begin(), tree_ids.end()) + 1;
}

const double RayTracer::bounding_box_bump(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  auto volume_bounding_box = mesh_manager->volume_bounding_box(volume_id);
  return std::max(volume_bounding_box.dilation(), numerical_precision_);
}

} // namespace xdg