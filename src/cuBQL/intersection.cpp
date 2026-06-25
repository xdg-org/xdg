#include <omp.h>

#include "xdg/cuBQL/intersection.h"
#include "xdg/geometry/plucker.h"
#include "xdg/error.h"

#include "cuBQL/math/Ray.h"
#include "cuBQL/traversal/rayQueries.h"

namespace xdg {

// Core traversal and intersection routine for a single ray against a given volume tlas
#pragma omp declare target
static inline void intersect_surface_tree(CuBQLVolumeTLAS::DD volume_tlas,
                                          CuBQLRay intersection_ray,
                                          CuBQLSurfaceHit* hit,
                                          int orientation,
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

  CuBQLVolumeTLAS::SurfaceInstanceDD surface_instance;

  auto enter_blas = [=, &surface_instance, &traversal_ray]
    (cuBQL::ray3f& out_ray, cuBQL::bvh3f& out_bvh, int instance_id)
  {
    surface_instance = volume_tlas.surface_instances[instance_id];
    out_ray = traversal_ray;
    out_bvh = surface_instance.surface_blas.bvh;
  };

  auto intersect_prim = [=, &traversal_ray, &surface_instance]
    (uint32_t prim_id) -> float
  {
    const CuBQLSurfaceMesh::DD mesh = surface_instance.surface_blas.mesh;
    const MeshID primitive_ref = mesh.primitive_refs[prim_id];

    for (int i = 0; i < exclude_count; ++i) {
      if (exclude_primitives[i] == primitive_ref) {
        return traversal_ray.tMax;
      }
    }

    const cuBQL::vec3i index = mesh.indices[prim_id];

    cuBQL::vec3d vertices[3] = {
      mesh.vertices[index.x],
      mesh.vertices[index.y],
      mesh.vertices[index.z]
    };

    cuBQL::vec3d normal = cuBQL::cross(vertices[1] - vertices[0],
                                       vertices[2] - vertices[0]);

    if (surface_instance.reverse_sense) {
      normal = -normal;
    }

    const double normal_dot_direction = dot(normal, intersection_ray.direction);

    if (orientation_cull(normal_dot_direction,
                         static_cast<HitOrientation>(orientation))) {
      return traversal_ray.tMax;
    }

    auto intersection = plucker_ray_tri_intersect(vertices,
                                                  intersection_ray.origin,
                                                  intersection_ray.direction,
                                                  hit->distance,
                                                  intersection_ray.tMin,
                                                  false,
                                                  0);

    if (intersection.hit) {
      hit->distance = intersection.t;
      hit->surface = mesh.surface_id;
      hit->primitive = primitive_ref;
      hit->piv = normal_dot_direction > 0.0 ? INSIDE : OUTSIDE;
      traversal_ray.tMax = static_cast<float>(intersection.t);
    }

    // Return value is only the FP32 traversal shrink distance. The accepted hit
    // distance stored above remains the FP64 Plucker result.
    return traversal_ray.tMax;
  };

  auto leave_blas = []() -> void {};

  cuBQL::shrinkingRayQuery::twoLevel::forEachPrim(enter_blas,
                                                  leave_blas,
                                                  intersect_prim,
                                                  volume_tlas.bvh,
                                                  traversal_ray);
}
#pragma omp end declare target

void
intersect_surface_tree_scalar(const cubql::Context& context,
                              const CuBQLVolumeTLAS& volume_tlas,
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

  const auto volume_tlas_dd = volume_tlas.get_device_data();
  const int orientation = static_cast<int>(hit_orientation);

  #pragma omp target device(gpu_id) \
    is_device_ptr(d_exclude_primitives, d_surface_hit)
  {
    intersect_surface_tree(volume_tlas_dd,
                           ray,
                           d_surface_hit,
                           orientation,
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
                             const CuBQLVolumeTLAS::DD* d_volume_to_tlas,
                             XDGRayHit* d_ray_hits,
                             std::size_t num_rays,
                             HitOrientation hit_orientation)
{
  if (num_rays == 0) return;

  if (!d_volume_to_tlas || !d_ray_hits) {
    fatal_error("Invalid cuBQL batch intersection buffers");
  }

  const int gpu_id = context.gpuID;

  #pragma omp target teams distribute parallel for device(gpu_id) \
    is_device_ptr(d_volume_to_tlas, d_ray_hits)
  for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
    XDGRayHit ray_hit = d_ray_hits[ray_id];

    CuBQLSurfaceHit hit;
    hit.distance = ray_hit.t_max;
    hit.surface = ID_NONE;
    hit.primitive = ID_NONE;
    hit.piv = OUTSIDE;

    if (ray_hit.volume != ID_NONE) {
      CuBQLRay ray;
      ray.origin = cuBQL::vec3d(ray_hit.origin[0],
                                ray_hit.origin[1],
                                ray_hit.origin[2]);
      ray.direction = cuBQL::vec3d(ray_hit.direction[0],
                                   ray_hit.direction[1],
                                   ray_hit.direction[2]);
      ray.tMin = ray_hit.t_min;
      ray.tMax = ray_hit.t_max;
      ray.volume = ray_hit.volume;

      const CuBQLVolumeTLAS::DD volume_tlas = d_volume_to_tlas[ray.volume];

      intersect_surface_tree(volume_tlas,
                             ray,
                             &hit,
                             static_cast<int>(hit_orientation),
                             nullptr,
                             0);
    }

    ray_hit.distance = hit.distance;
    ray_hit.surface = hit.surface;
    ray_hit.primitive = hit.primitive;
    ray_hit.point_in_volume = static_cast<std::int32_t>(hit.piv);
    d_ray_hits[ray_id] = ray_hit;
  }
}

} // namespace xdg
