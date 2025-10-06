#include <algorithm> // for find

#include "xdg/geometry/closest.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"
#include "xdg/geometry/plucker.h"
#include "xdg/ray.h"

namespace xdg
{

bool orientation_cull(const Direction& ray_dir, const Direction& normal, HitOrientation orientation)
{
  if (orientation == HitOrientation::ANY) return false;

  double dot_prod = ray_dir.dot(normal);
  if (orientation == HitOrientation::EXITING && dot_prod < 0.0) {
    return true;
  }
  else if (orientation == HitOrientation::ENTERING && dot_prod >= 0.0) {
    return true;
  }
  return false;
}

bool primitive_mask_cull(RTCDualRayHit* rayhit, int primID) {
  if (!rayhit->ray.exclude_primitives) return false;

  RTCSurfaceDualRay& ray = rayhit->ray;
  RTCDualHit& hit = rayhit->hit;

  // if the primitive mask is set, cull if the primitive is not in the mask
  return std::find(ray.exclude_primitives->begin(), ray.exclude_primitives->end(), primID) != ray.exclude_primitives->end();
}

void TriangleBoundsFunc(RTCBoundsFunctionArguments* args)
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

void TriangleIntersectionFunc(RTCIntersectFunctionNArguments* args) {
  const SurfaceUserData* user_data = (const SurfaceUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef& primitive_ref = user_data->prim_ref_buffer[args->primID];

  auto vertices = mesh_manager->face_vertices(primitive_ref.primitive_id);

  RTCDualRayHit* rayhit = (RTCDualRayHit*)args->rayhit;
  RTCSurfaceDualRay& ray = rayhit->ray;
  RTCDualHit& hit = rayhit->hit;

  Position ray_origin = {ray.dorg[0], ray.dorg[1], ray.dorg[2]};
  Direction ray_direction = {ray.ddir[0], ray.ddir[1], ray.ddir[2]};

  // local variable for distance to the triangle intersection
  auto result = plucker_ray_tri_intersect(vertices.data(), 
                                          ray_origin, 
                                          ray_direction,
                                          rayhit->ray.dtfar,
                                          0.0,
                                          false,
                                          0);
  double plucker_dist = result.t;

  if (!result.hit) return;

  if (plucker_dist > rayhit->ray.dtfar) return;

  Direction normal = mesh_manager->face_normal(primitive_ref.primitive_id);

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
  auto vertices = mesh_manager->face_vertices(primitive_ref.primitive_id);

  RTCDPointQuery* query = (RTCDPointQuery*) args->query;
  Position p {query->dblx, query->dbly, query->dblz};

  Position result = closest_location_on_triangle(vertices, p);

  double dist = (result - p).length();
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

  auto vertices = mesh_manager->face_vertices(primitive_ref.primitive_id);

  // get the double precision ray from the args
  RTCSurfaceDualRay* ray = (RTCSurfaceDualRay*) args->ray;

  auto result = plucker_ray_tri_intersect(vertices.data(), 
                                          ray->dorg, 
                                          ray->ddir,
                                          ray->dtfar,
                                          0.0,
                                          false,
                                          0);
  double plucker_dist = result.t;

  if (result.hit) {
    ray->set_tfar(-INFTY);
  }
}

} // namespace xdg


