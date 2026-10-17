#include "xdg/embree/tetrahedron_callbacks.h"

#include "xdg/constants.h"
#include "xdg/geometry/tetrahedron_contain.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/embree/ray.h"

namespace xdg
{

void TetrahedronIntersectionFunc(RTCIntersectFunctionNArguments* args) {
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  // TODO: Update this!
  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);

  RTCDualRayHit* rayhit = (RTCDualRayHit*)args->rayhit;
  RTCSurfaceDualRay& ray = rayhit->ray;
  RTCDualHit& hit = rayhit->hit;

  Position ray_origin = {ray.dorg[0], ray.dorg[1], ray.dorg[2]};

  // check the containment of the point
  bool inside = tet_containment_test(ray_origin, vertices[0], vertices[1], vertices[2], vertices[3]);

  if (!inside) return;
  // zero out the hit information
  rayhit->hit.u = 0.0;
  rayhit->hit.v = 0.0;
  rayhit->hit.Ng_x = 0.0;
  rayhit->hit.Ng_y = 0.0;
  rayhit->hit.Ng_z = 0.0;
  // set the hit information
  rayhit->hit.geomID = args->geomID;
  rayhit->hit.primID = args->primID;
}

void TetrahedronOcclusionFunc(RTCOccludedFunctionNArguments* args)
{
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];

  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);

  RTCElementDualRay* ray = (RTCElementDualRay*)args->ray;
  Position ray_origin = {ray->dorg[0], ray->dorg[1], ray->dorg[2]};

  // check the containment of the point
  bool inside = tet_containment_test(ray_origin, vertices[0], vertices[1], vertices[2], vertices[3]);

  if (!inside) return;

  // set the hit information
  ray->element = primitive_ref.primitive_id;
  ray->set_tfar(-INFTY);
}

} // namespace xdg