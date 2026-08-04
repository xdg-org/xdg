#include <omp.h>

#include "xdg/cuBQL/intersection.h"
#include "xdg/geometry/plucker.h"
#include "xdg/error.h"

#include "cuBQL/math/Ray.h"
#include "cuBQL/traversal/rayQueries.h"

namespace xdg {

// Core traversal and intersection routine for a single ray against a flattened
// volume group.
#pragma omp declare target
static inline float reject_candidate(const cuBQL::ray3f& traversal_ray)
{
  return traversal_ray.tMax;
}

static inline void intersect_surface_tree(CuBQLVolumeGroup::DD volume_group,
                                          CuBQLRay intersection_ray,
                                          CuBQLSurfaceHit* hit,
                                          int orientation,
                                          MeshID last_hit_primitive,
                                          const MeshID* exclude_primitives,
                                          int exclude_count)
{
  // cuBQL traverses the FP32 BVH with an FP32 ray; the original CuBQLRay
  // remains the FP64 source of truth for the final triangle intersection.
  cuBQL::ray3f traversal_ray;
  traversal_ray.origin = cuBQL::vec3f(intersection_ray.origin);
  traversal_ray.direction = cuBQL::vec3f(intersection_ray.direction);
  // TODO: Is this truncation safe enough for tmin and tmax? Pretty sure embree/gprt does a similar truncation for ray bounds
  traversal_ray.tMin = static_cast<float>(intersection_ray.tMin);
  traversal_ray.tMax = static_cast<float>(hit->distance);

  auto intersect_prim = [=, &traversal_ray]
    (std::uint32_t bvh_primitive_index) -> float
  {
    const auto ref = volume_group.prim_refs[bvh_primitive_index];
    const auto surface = volume_group.surfaces[ref.surface_index];
    const auto mesh = surface.mesh;
    const auto local_index = ref.primitive_index;
    const MeshID primitive_id = mesh.primitive_ids[local_index];

    // Reject the previously hit primitive to avoid immediate self-intersection.
    if (primitive_id == last_hit_primitive) {
      return reject_candidate(traversal_ray);
    }

    // Scalar queries may provide an arbitrary primitive exclusion history.
    // TODO - Think about how to provide arbitrary history checks for batch queries.
    for (int i = 0; i < exclude_count; ++i) {
      if (exclude_primitives[i] == primitive_id) {
        return reject_candidate(traversal_ray);
      }
    }

    const cuBQL::vec3i vertex_indices = mesh.indices[local_index];

    cuBQL::vec3d vertices[3] = {
      mesh.vertices[vertex_indices.x],
      mesh.vertices[vertex_indices.y],
      mesh.vertices[vertex_indices.z]
    };

    cuBQL::vec3d normal = cuBQL::cross(vertices[1] - vertices[0],
                                       vertices[2] - vertices[0]);

    double normal_dot_direction = dot(normal, intersection_ray.direction);

    if (surface.reverse_sense) {
      normal_dot_direction = -normal_dot_direction;
    }

    if (orientation_cull(normal_dot_direction, static_cast<HitOrientation>(orientation))) {
      return reject_candidate(traversal_ray);
    }

    auto intersection = plucker_ray_tri_intersect(vertices,
                                                  intersection_ray.origin,
                                                  intersection_ray.direction,
                                                  hit->distance,
                                                  intersection_ray.tMin,
                                                  false,
                                                  0);
    
    // store ray payload if hit found
    if (intersection.hit) {
      hit->distance = intersection.t;
      hit->surface = mesh.surface_id;
      hit->primitive = primitive_id;
      hit->piv = normal_dot_direction > 0.0 ? INSIDE : OUTSIDE;
      hit->next_volume = surface.next_volume;
      hit->boundary_condition = surface.boundary_condition;
      hit->normal = normal;
      traversal_ray.tMax = static_cast<float>(intersection.t);
    }

    // Return value is only the FP32 traversal shrink distance. The accepted hit
    // distance stored above remains the FP64 Plucker result.
    return reject_candidate(traversal_ray);
  };

  // Single level traversal call for a shrinking ray query against the flattened BVH of the volume group.
  cuBQL::shrinkingRayQuery::forEachPrim(intersect_prim, volume_group.bvh, traversal_ray);
}
#pragma omp end declare target

void
intersect_surface_tree_scalar(const cubql::Context& context,
                              const CuBQLVolumeGroup& volume_group,
                              const CuBQLRay& ray,
                              CuBQLSurfaceHit& surface_hit,
                              HitOrientation hit_orientation,
                              const std::vector<MeshID>* exclude_primitives)
{
  const int gpu_id = context.gpuID;

  MeshID* d_exclude_primitives = nullptr;
  int exclude_count = 0;
  if (exclude_primitives && !exclude_primitives->empty()) {
    exclude_count = static_cast<int>(exclude_primitives->size());
    d_exclude_primitives = static_cast<MeshID*>
      (omp_target_alloc(exclude_count * sizeof(MeshID), gpu_id));

    omp_target_memcpy(d_exclude_primitives,
                      exclude_primitives->data(),
                      exclude_count * sizeof(MeshID),
                      0,
                      0,
                      gpu_id,
                      context.hostID);
  }

  auto* d_surface_hit = static_cast<CuBQLSurfaceHit*>
    (omp_target_alloc(sizeof(CuBQLSurfaceHit), gpu_id));

  surface_hit.distance = ray.tMax;
  omp_target_memcpy(d_surface_hit,
                    &surface_hit,
                    sizeof(CuBQLSurfaceHit),
                    0,
                    0,
                    gpu_id,
                    context.hostID);

  const auto volume_group_dd = volume_group.get_device_data();
  const int orientation = static_cast<int>(hit_orientation);

  #pragma omp target device(gpu_id) \
    is_device_ptr(d_exclude_primitives, d_surface_hit)
  {
    intersect_surface_tree(volume_group_dd,
                           ray,
                           d_surface_hit,
                           orientation,
                           ID_NONE,
                           d_exclude_primitives,
                           exclude_count);
  }

  omp_target_memcpy(&surface_hit,
                    d_surface_hit,
                    sizeof(CuBQLSurfaceHit),
                    0,
                    0,
                    context.hostID,
                    gpu_id);

  omp_target_free(d_surface_hit, gpu_id);

  if (d_exclude_primitives) {
    omp_target_free(d_exclude_primitives, gpu_id);
  }

  return;
}

void
intersect_surface_tree_batch(const cubql::Context& context,
                             const CuBQLVolumeGroup::DD* d_volume_to_group,
                             XDGRayHit* d_ray_hits,
                             std::size_t num_rays,
                             HitOrientation hit_orientation)
{
  if (num_rays == 0) return;

  if (!d_volume_to_group || !d_ray_hits) {
    fatal_error("Invalid cuBQL batch intersection buffers");
  }

  const int gpu_id = context.gpuID;

  #pragma omp target teams distribute parallel for device(gpu_id) \
    is_device_ptr(d_volume_to_group, d_ray_hits)
  for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
    const XDGRayHit ray_hit = d_ray_hits[ray_id];

    CuBQLSurfaceHit hit;
    hit.distance = ray_hit.t_max;
    hit.surface = ID_NONE;
    hit.primitive = ID_NONE;
    hit.piv = OUTSIDE;
    hit.next_volume = ID_NONE;

    if (ray_hit.volume != ID_NONE) {
      CuBQLRay ray;
      ray.origin = cuBQL::vec3d(ray_hit.origin[0], ray_hit.origin[1], ray_hit.origin[2]);
      ray.direction = cuBQL::vec3d(ray_hit.direction[0], ray_hit.direction[1], ray_hit.direction[2]);
      ray.tMin = ray_hit.t_min;
      ray.tMax = ray_hit.t_max;
      ray.volume = ray_hit.volume;

      const CuBQLVolumeGroup::DD volume_group = d_volume_to_group[ray.volume];

      intersect_surface_tree(volume_group,
                             ray,
                             &hit,
                             static_cast<int>(hit_orientation),
                             ray_hit.last_hit_primitive,
                             nullptr,
                             0);
    }

    d_ray_hits[ray_id].distance = hit.distance;
    d_ray_hits[ray_id].surface = hit.surface;
    d_ray_hits[ray_id].primitive = hit.primitive;
    d_ray_hits[ray_id].point_in_volume = static_cast<std::int32_t>(hit.piv);
    d_ray_hits[ray_id].next_volume = hit.next_volume;
    d_ray_hits[ray_id].boundary_condition = static_cast<std::int32_t>(hit.boundary_condition);
    d_ray_hits[ray_id].normal[0] = hit.normal.x;
    d_ray_hits[ray_id].normal[1] = hit.normal.y;
    d_ray_hits[ray_id].normal[2] = hit.normal.z;
  }
}

} // namespace xdg
