
#include "xdg/bbox.h"
#include "xdg/bounding_functions.h"
#include "xdg/geometry_data.h"
#include "xdg/primitive_ref.h"
#include "xdg/mesh_manager_interface.h"

namespace xdg
{
  void VolumeElementBoundsFunc(RTCBoundsFunctionArguments* args)
  {
    const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
    const MeshManager* mesh_manager = user_data->mesh_manager;

    const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];

    BoundingBox bounds = mesh_manager->element_bounding_box(primitive_ref.primitive_id);
    double bump = bounds.dilation();

    args->bounds_o->lower_x = bounds.min_x - bump;
    args->bounds_o->lower_y = bounds.min_y - bump;
    args->bounds_o->lower_z = bounds.min_z - bump;
    args->bounds_o->upper_x = bounds.max_x + bump;
    args->bounds_o->upper_y = bounds.max_y + bump;
    args->bounds_o->upper_z = bounds.max_z + bump;
  }

  void SurfaceFaceBoundsFunc(RTCBoundsFunctionArguments* args)
  {
  const SurfaceUserData* user_data = (const SurfaceUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];
  BoundingBox bounds = mesh_manager->face_bounding_box(primitive_ref.primitive_id);

  args->bounds_o->lower_x = bounds.min_x - user_data->box_bump;
  args->bounds_o->lower_y = bounds.min_y - user_data->box_bump;
  args->bounds_o->lower_z = bounds.min_z - user_data->box_bump;
  args->bounds_o->upper_x = bounds.max_x + user_data->box_bump;
  args->bounds_o->upper_y = bounds.max_y + user_data->box_bump;
  args->bounds_o->upper_z = bounds.max_z + user_data->box_bump;
  }
} // namespace xdg
