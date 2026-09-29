#include "xdg/embree/hexahedron_callbacks.h"
#include "xdg/geometry/hexahedron_contain.h"

#include "xdg/constants.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/embree/ray.h"

namespace xdg
{


void HexahedronIntersectionFunc(RTCIntersectFunctionNArguments* args)
{
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);
  if (vertices.size() != 8) {
    fatal_error("HexahedronIntersectionFunc expected 8 vertices, got {}", vertices.size());
  }

  std::array<Vertex, 8> verts;
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i] = vertices[i];
  }

  RTCDualRayHit* rayhit = (RTCDualRayHit*)args->rayhit;
  RTCSurfaceDualRay& ray = rayhit->ray;

  Position ray_origin = {ray.dorg[0], ray.dorg[1], ray.dorg[2]};

  bool inside = hex_containment_test(ray_origin, verts);
  if (!inside) return;

  rayhit->hit.u = 0.0;
  rayhit->hit.v = 0.0;
  rayhit->hit.Ng_x = 0.0;
  rayhit->hit.Ng_y = 0.0;
  rayhit->hit.Ng_z = 0.0;
  rayhit->hit.geomID = args->geomID;
  rayhit->hit.primID = args->primID;
}

void HexahedronOcclusionFunc(RTCOccludedFunctionNArguments* args)
{
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);
  if (vertices.size() != 8) {
    fatal_error("HexahedronOcclusionFunc expected 8 vertices, got {}", vertices.size());
  }

  std::array<Vertex, 8> verts;
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i] = vertices[i];
  }

  RTCElementDualRay* ray = (RTCElementDualRay*)args->ray;
  Position ray_origin = {ray->dorg[0], ray->dorg[1], ray->dorg[2]};

  bool inside = hex_containment_test(ray_origin, verts);
  if (!inside) return;

  // indicate that the query is complete due to a successful containment result
  ray->element = primitive_ref.primitive_id;
  ray->set_tfar(-INFTY);
}

} // namespace xdg
