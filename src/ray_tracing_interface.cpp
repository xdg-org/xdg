#include <algorithm>
#include "xdg/ray_tracing_interface.h"
#include "xdg/error.h"

// Any methods which are identical for all RT backends should be defined here

namespace xdg {

RayTracer::~RayTracer() {}

XDGRayHitBuffer RayTracer::allocate_ray_hits(std::size_t) const
{
  fatal_error("Selected ray tracer does not support device batch ray fire");
  return {};
}

void RayTracer::free_ray_hits(XDGRayHitBuffer&) const
{
  fatal_error("Selected ray tracer does not support device batch ray fire");
}

void RayTracer::ray_fire_batch(const XDGRayHitBuffer&,
                               HitOrientation) const
{
  fatal_error("Selected ray tracer does not support device batch ray fire");
}

SurfaceTreeID RayTracer::next_surface_tree_id()
{
  return ++next_surface_tree_id_;
}

ElementTreeID RayTracer::next_element_tree_id()
{
  return ++next_element_tree_id_;
}

const double RayTracer::bounding_box_bump(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  auto volume_bounding_box = mesh_manager->volume_bounding_box(volume_id);
  return std::max(volume_bounding_box.dilation(), numerical_precision_);
}

} // namespace xdg
