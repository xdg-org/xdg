#include "xdg/constants.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/ray.h"
#include "xdg/vec3da.h"

#include "xdg/util/linalg.h"

namespace xdg
{

bool plucker_tet_containment_test(const Position& point,
                                  const Position& v0,
                                  const Position& v1,
                                  const Position& v2,
                                  const Position& v3) {
    using linalg::aliases::double3x3;
    using linalg::aliases::double3;
    using linalg::aliases::double4;
    // Create matrix T = [v1 - v0, v2 - v0, v3 - v0]
    Vec3da e0 = v1 - v0;
    Vec3da e1 = v2 - v0;
    Vec3da e2 = v3 - v0;
    double3x3 T = { {e0.x, e0.y, e0.z},
                   {e1.x, e1.y, e1.z},
                   {e2.x, e2.y, e2.z}};

    // Vector from v0 to point
    Vec3da rhs = point - v0;

    // Solve T * [λ1, λ2, λ3] = rhs
    double3 lambda123 = mul(inverse(T),{rhs.x, rhs.y, rhs.z});

    // Compute λ0
    double lambda0 = 1.0f - (lambda123.x + lambda123.y + lambda123.z);

    // Final barycentric coordinate vector
    double4 bary = { lambda0, lambda123.x, lambda123.y, lambda123.z };

    // Check all λ_i in [0, 1]
    for (int i = 0; i < 4; ++i) {
        if (bary[i] < -PLUCKER_ZERO_TOL || bary[i] > 1.0f + PLUCKER_ZERO_TOL)
            return false;
    }
    return true;
}

// Embree callbacks

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
  bool inside = plucker_tet_containment_test(ray_origin, vertices[0], vertices[1], vertices[2], vertices[3]);

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
  bool inside = plucker_tet_containment_test(ray_origin, vertices[0], vertices[1], vertices[2], vertices[3]);

  if (!inside) return;

  // set the hit information
  ray->element = primitive_ref.primitive_id;
  ray->set_tfar(-INFTY);
}

} // namespace xdg