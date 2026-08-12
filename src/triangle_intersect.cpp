#include <array>

#include "xdg/constants.h"
#include "xdg/embree_interface.h"
#include "xdg/geometry/closest.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"
#include "xdg/geometry/face_common.h"
#include "xdg/geometry/plucker.h"
#include "xdg/intersect_common.h"
#include "xdg/ray.h"

namespace xdg
{

void TriangleIntersectionFunc(RTCIntersectFunctionNArguments* args) {
  const SurfaceUserData* user_data = (const SurfaceUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];

  auto face_vertices = mesh_manager->face_vertex_coordinates(primitive_ref.primitive_id);

  RTCDualRayHit* rayhit = (RTCDualRayHit*)args->rayhit;
  RTCSurfaceDualRay& ray = rayhit->ray;

  Position ray_origin = {ray.dorg[0], ray.dorg[1], ray.dorg[2]};
  Direction ray_direction = {ray.ddir[0], ray.ddir[1], ray.ddir[2]};

  // local variable for distance to the triangle intersection
  auto result = plucker_ray_tri_intersect(face_vertices.data(),
                                          ray_origin,
                                          ray_direction,
                                          rayhit->ray.dtfar,
                                          0.0,
                                          false,
                                          0);
  if (!result.hit) return;
  double plucker_dist = result.t;

  if (plucker_dist > rayhit->ray.dtfar) return;

  Direction normal = triangle_normal({face_vertices[0], face_vertices[1], face_vertices[2]});

  // Check if ray is entering or exiting the volume it was fired against
  // if this is a normal ray fire, flip the normal as needed
  if (ray.volume_tree == user_data->reverse_vol && rayhit->ray.rf_type != RayFireType::FIND_VOLUME)
  {
    normal = -normal;
  }

  if (rayhit->ray.rf_type == RayFireType::VOLUME) {
   if (orientation_cull(rayhit->ray.ddir, normal, rayhit->ray.orientation)) return;
   if (primitive_mask_cull(rayhit, primitive_ref.primitive_id)) return;
  }


  // if we've gotten through all of the filters, set the ray information
  rayhit->ray.set_tfar(plucker_dist);
  // zero-out barycentric coords
  rayhit->hit.u = 0.0;
  rayhit->hit.v = 0.0;
  rayhit->hit.Ng_x = 0.0;
  rayhit->hit.Ng_y = 0.0;
  rayhit->hit.Ng_z = 0.0;
  // set the hit information
  rayhit->hit.geomID = args->geomID;
  rayhit->hit.primID = args->primID;
  rayhit->hit.primitive_ref = &primitive_ref;
  rayhit->hit.surface = user_data->surface_id;
  rayhit->hit.dNg = normal;
}

bool TriangleClosestFunc(RTCPointQueryFunctionArguments* args) {
  RTCGeometry g = rtcGetGeometry(*(RTCScene*)args->userPtr, args->geomID);
  // get the array of DblTri's stored on the geometry
  const SurfaceUserData* user_data = (const SurfaceUserData*) rtcGetGeometryUserData(g);

  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto face_vertices = mesh_manager->face_vertex_coordinates(primitive_ref.primitive_id);

  RTCDPointQuery* query = (RTCDPointQuery*) args->query;
  Position p {query->dblx, query->dbly, query->dblz};

  Position result;
  double dist = INFTY;

  std::array<Vertex, 3> tri {face_vertices[0], face_vertices[1], face_vertices[2]};
  result = closest_location_on_triangle(tri, p);
  dist = (result - p).length();

  if ( dist < query->dradius) {
    query->radius = dist;
    query->dradius = dist;
    query->primitive_ref = &primitive_ref;
    query->primID = args->primID;
    query->geomID = args->geomID;
    return true;
  } else {
    return false;
  }
}

void TriangleOcclusionFunc(RTCOccludedFunctionNArguments* args) {
  const SurfaceUserData* user_data = (const SurfaceUserData*) args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;
  const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];

  auto face_vertices = mesh_manager->face_vertex_coordinates(primitive_ref.primitive_id);

  // get the double precision ray from the args
  RTCSurfaceDualRay* ray = (RTCSurfaceDualRay*) args->ray;

  auto result = plucker_ray_tri_intersect(face_vertices.data(),
                                          ray->dorg,
                                          ray->ddir,
                                          ray->dtfar,
                                          0.0,
                                          false,
                                          0);

  if (result.hit) ray->set_tfar(-INFTY);
}

} // namespace xdg
