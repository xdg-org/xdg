#include "xdg/cuBQL/ray_tracer.h"
#include "xdg/cuBQL/intersection.h"
#include "xdg/error.h"
#include "xdg/geometry/plucker.h"
#include "xdg/available_device_probe.h"

#include <omp.h>
#include "cuBQL/builder/omp.h"
#include "cuBQL/math/Ray.h"
#include "cuBQL/queries/triangleData/Triangle.h"
#include "cuBQL/queries/triangleData/math/rayTriangleIntersections.h"
#include "cuBQL/traversal/rayQueries.h"

#include <algorithm>
#include <limits>

namespace xdg {

CuBQLRayTracer::CuBQLRayTracer()
{
  if (!system_has_omp_target_device()) {
    fatal_error("No OpenMP target capable device found; cannot initialize cuBQL ray tracer.");
  }

  context_.gpuID = 0; // TODO - support selecting among multiple OpenMP target devices.
  context_.hostID = omp_get_initial_device();
}

CuBQLRayTracer::~CuBQLRayTracer()
{
  if (d_volume_to_group_) {
    omp_target_free(d_volume_to_group_, context_.gpuID);
    d_volume_to_group_ = nullptr;
  }

  for (auto& [tree, group] : tree_to_volume_group_) {
    group.release();
  }

  for (auto& [surface, mesh] : surface_to_mesh_) {
    mesh.release();
  }
}

void CuBQLRayTracer::init()
{
  upload_volume_to_group_table_();
  initialized_ = true;
}

void CuBQLRayTracer::upload_volume_to_group_table_()
{
  if (d_volume_to_group_) {
    omp_target_free(d_volume_to_group_, context_.gpuID);
    d_volume_to_group_ = nullptr;
  }

  if (volume_to_group_.empty()) {
    return;
  }

  d_volume_to_group_ = static_cast<CuBQLVolumeGroup::DD*>
    (omp_target_alloc(volume_to_group_.size() * sizeof(CuBQLVolumeGroup::DD),
                      context_.gpuID));
  omp_target_memcpy(d_volume_to_group_,
                    volume_to_group_.data(),
                    volume_to_group_.size() * sizeof(CuBQLVolumeGroup::DD),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);
}

std::pair<TreeID, TreeID>
CuBQLRayTracer::register_volume(const std::shared_ptr<MeshManager>& mesh_manager,
                                MeshID volume)
{
  TreeID surface_tree = create_surface_tree(mesh_manager, volume);
  TreeID element_tree = create_element_tree(mesh_manager, volume);
  return {surface_tree, element_tree};
}

CuBQLSurfaceMesh
CuBQLRayTracer::register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                                  MeshID surface_id)
{
  const auto num_faces = mesh_manager->num_surface_faces(surface_id);
  auto vertices = mesh_manager->get_surface_vertices(surface_id);
  auto indices = mesh_manager->get_surface_connectivity(surface_id);
  auto h_primitive_ids = mesh_manager->get_surface_faces(surface_id);

  std::vector<cuBQL::vec3d> h_vertices;
  h_vertices.reserve(vertices.size());
  for (const auto& vertex : vertices) {
    h_vertices.emplace_back(vertex.x, vertex.y, vertex.z);
  }

  std::vector<cuBQL::vec3i> h_indices;
  h_indices.reserve(indices.size() / 3);
  for (size_t i = 0; i < indices.size(); i += 3) {
    h_indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
  }

  // TODO- think about how to better handle omp transfer calls. AutoUploadArrays is one option
  auto* d_vertices = static_cast<cuBQL::vec3d*>
    (omp_target_alloc(h_vertices.size() * sizeof(cuBQL::vec3d), context_.gpuID));
  omp_target_memcpy(d_vertices,
                    h_vertices.data(),
                    h_vertices.size() * sizeof(cuBQL::vec3d),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  auto* d_indices = static_cast<cuBQL::vec3i*>
    (omp_target_alloc(h_indices.size() * sizeof(cuBQL::vec3i), context_.gpuID));
  omp_target_memcpy(d_indices,
                    h_indices.data(),
                    h_indices.size() * sizeof(cuBQL::vec3i),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  auto* d_primitive_ids = static_cast<MeshID*>
    (omp_target_alloc(h_primitive_ids.size() * sizeof(MeshID), context_.gpuID));
  omp_target_memcpy(d_primitive_ids,
                    h_primitive_ids.data(),
                    h_primitive_ids.size() * sizeof(MeshID),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  CuBQLSurfaceMesh surface_mesh;
  surface_mesh.surface_id = surface_id;
  surface_mesh.d_vertices = d_vertices;
  surface_mesh.d_indices = d_indices;
  surface_mesh.d_primitive_ids = d_primitive_ids;
  surface_mesh.num_vertices = static_cast<std::uint32_t>(h_vertices.size());
  surface_mesh.num_primitives = static_cast<std::uint32_t>(num_faces);
  surface_mesh.gpu_id = context_.gpuID;

  return surface_mesh;
}

TreeID
CuBQLRayTracer::create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                                     MeshID volume_id)
{
  // TODO - Right now each CuBQLRayTracer instance has a single "Context" which holds a single GPU_ID
  // so this will need to be reworked in the future to handle multi-gpus

  SurfaceTreeID tree = next_surface_tree_id();
  surface_trees_.push_back(tree);
  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);

  if (volume_surfaces.empty()) {
    fatal_error("Volume {} has no surfaces; cannot build cuBQL surface tree", volume_id);
  }

  const double volume_bump = bounding_box_bump(mesh_manager, volume_id);

  std::uint64_t num_volume_primitives = 0;
  for (const MeshID surface : volume_surfaces) {
    if (!surface_to_mesh_.count(surface)) {
      surface_to_mesh_.emplace(surface, register_surface(mesh_manager, surface));
    }

    auto& surface_mesh = surface_to_mesh_.at(surface);
    surface_mesh.max_parent_volume_bump = std::max(surface_mesh.max_parent_volume_bump, volume_bump);
    num_volume_primitives += surface_mesh.num_primitives;
  }

  std::vector<CuBQLVolumeGroup::SurfaceDD> h_surfaces;
  std::vector<CuBQLVolumeGroup::PrimRef> h_prim_refs;
  h_surfaces.reserve(volume_surfaces.size());
  h_prim_refs.reserve(static_cast<std::size_t>(num_volume_primitives));

  for (const MeshID surface : volume_surfaces) {
    const auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surface);
    const CuBQLSurfaceMesh& surface_mesh = surface_to_mesh_.at(surface);

    CuBQLVolumeGroup::SurfaceDD surface_data;
    surface_data.mesh = surface_mesh.get_device_data();

    if (volume_id == forward_parent) {
      surface_data.next_volume = reverse_parent;
    } else if (volume_id == reverse_parent) {
      surface_data.reverse_sense = true;
      surface_data.next_volume = forward_parent;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surface);
    }

    const auto property = mesh_manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION);
    if (property.value == "vacuum") {
      surface_data.boundary_condition = VACUUM;
    } else if (property.value == "reflecting" || property.value == "reflective") {
      surface_data.boundary_condition = REFLECTIVE;
    } else if (property.value == "transmission") {
      surface_data.boundary_condition = TRANSMISSION;
    } else {
      fatal_error("Unsupported boundary condition '{}' on surface {}", property.value, surface);
    }

    const auto surface_index = static_cast<std::uint32_t>(h_surfaces.size());
    h_surfaces.push_back(surface_data);

    for (std::uint32_t prim_index = 0; prim_index < surface_mesh.num_primitives; ++prim_index) {
      h_prim_refs.push_back({surface_index, prim_index});
    }
  }

  auto* d_aabbs = static_cast<cuBQL::box3f*>
    (omp_target_alloc(h_prim_refs.size() * sizeof(cuBQL::box3f),
                      context_.gpuID));
  auto* d_surfaces = static_cast<CuBQLVolumeGroup::SurfaceDD*>
    (omp_target_alloc(h_surfaces.size() * sizeof(CuBQLVolumeGroup::SurfaceDD),
                      context_.gpuID));
  omp_target_memcpy(d_surfaces,
                    h_surfaces.data(),
                    h_surfaces.size() * sizeof(CuBQLVolumeGroup::SurfaceDD),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  auto* d_prim_refs = static_cast<CuBQLVolumeGroup::PrimRef*>
    (omp_target_alloc(h_prim_refs.size() * sizeof(CuBQLVolumeGroup::PrimRef),
                      context_.gpuID));
  omp_target_memcpy(d_prim_refs,
                    h_prim_refs.data(),
                    h_prim_refs.size() * sizeof(CuBQLVolumeGroup::PrimRef),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  const auto num_primitives = static_cast<std::uint32_t>(h_prim_refs.size());
  const int gpu_id = context_.gpuID;
  #pragma omp target teams distribute parallel for device(gpu_id) \
    is_device_ptr(d_aabbs, d_surfaces, d_prim_refs)
  for (std::uint32_t primID = 0; primID < num_primitives; ++primID) {
    const auto primitive = d_prim_refs[primID];
    const auto surface = d_surfaces[primitive.surface_index];
    const auto mesh = surface.mesh;
    const auto indices = mesh.indices[primitive.primitive_index];

    cuBQL::box3d aabb;
    aabb.extend(mesh.vertices[indices.x]);
    aabb.extend(mesh.vertices[indices.y]);
    aabb.extend(mesh.vertices[indices.z]);

    const cuBQL::vec3d bump(mesh.max_parent_volume_bump);
    aabb.lower = aabb.lower - bump;
    aabb.upper = aabb.upper + bump;
    d_aabbs[primID] = cuBQL::box3f(aabb);
  }

  CuBQLVolumeGroup volume_group;
  volume_group.d_surfaces = d_surfaces;
  volume_group.d_prim_refs = d_prim_refs;
  volume_group.num_surfaces = static_cast<std::uint32_t>(h_surfaces.size());
  volume_group.num_primitives = num_primitives;
  volume_group.gpu_id = context_.gpuID;

  cuBQL::BuildConfig build_params;
  cuBQL::build_omp_target(volume_group.bvh,
                          d_aabbs,
                          num_primitives,
                          build_params,
                          context_.gpuID);

  omp_target_free(d_aabbs, context_.gpuID);

  // Retain owning objects for scalar TreeID lookups and allocation lifetime.
  auto result = tree_to_volume_group_.emplace(tree, std::move(volume_group));
  auto it = result.first;

  // Keep a dense host-side MeshID -> group device-data table for batch queries.
  const auto volume_index = static_cast<size_t>(volume_id);
  if (volume_index >= volume_to_group_.size()) {
    volume_to_group_.resize(volume_index + 1);
  }
  volume_to_group_[volume_index] = it->second.get_device_data();

  if (initialized_) {
    upload_volume_to_group_table_();
  }
  
  return tree;
}

TreeID
CuBQLRayTracer::create_element_tree(const std::shared_ptr<MeshManager>&,
                                    MeshID)
{
  warning("Element trees not currently supported with cuBQL ray tracer");
  return TREE_NONE;
}

void CuBQLRayTracer::create_global_surface_tree()
{
  warning("Global surface trees not currently supported with cuBQL ray tracer");
}

void CuBQLRayTracer::create_global_element_tree()
{
  warning("Global element trees not currently supported with cuBQL ray tracer");
}

MeshID CuBQLRayTracer::find_element(const Position&) const
{
  fatal_error("Element queries not currently supported with cuBQL ray tracer");
  return ID_NONE;
}

MeshID CuBQLRayTracer::find_element(TreeID, const Position&) const
{
  fatal_error("Element queries not currently supported with cuBQL ray tracer");
  return ID_NONE;
}

bool CuBQLRayTracer::point_in_volume(TreeID tree,
                                     const Position& point,
                                     const Direction* direction,
                                     const std::vector<MeshID>* exclude_primitives) const
{
  const auto& context = context_;
  const CuBQLVolumeGroup& volume_group = tree_to_volume_group_.at(tree);

  // Use provided direction or if Direction == nulptr use default direction
  Direction directionUsed = (direction != nullptr) ? Direction{direction->x, direction->y, direction->z} 
                            : Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};
  
  CuBQLRay ray;
  ray.origin = cuBQL::vec3d(point.x, point.y, point.z);
  ray.direction = cuBQL::vec3d(directionUsed.x, directionUsed.y, directionUsed.z);
  ray.tMin = 0.0;
  ray.tMax = INFTY;

  CuBQLSurfaceHit surface_hit;
  
  // TODO - Maybe we can come up with a better name for this
  intersect_surface_tree_scalar(context, 
                                volume_group, 
                                ray, 
                                surface_hit,
                                HitOrientation::ANY, 
                                exclude_primitives);

  // if the ray hit nothing the point must be outside the volume
  if (surface_hit.primitive == ID_NONE) return false; 

  return surface_hit.piv == INSIDE;
}

std::pair<double, MeshID>
CuBQLRayTracer::ray_fire(TreeID tree,
                         const Position& origin,
                         const Direction& direction,
                         const double tmax,
                         HitOrientation hitOrientation,
                         std::vector<MeshID>* const exclude_primitives)
{
  const auto& context = context_;
  const CuBQLVolumeGroup& volume_group = tree_to_volume_group_.at(tree);

  CuBQLRay ray;
  ray.origin = cuBQL::vec3d(origin.x, origin.y, origin.z);
  ray.direction = cuBQL::vec3d(direction.x, direction.y, direction.z);
  ray.tMin = 0.0;
  ray.tMax = tmax;

  CuBQLSurfaceHit surface_hit;

  // TODO - Maybe we can come up with a better name for this
  intersect_surface_tree_scalar(context, 
                                volume_group, 
                                ray, 
                                surface_hit, 
                                hitOrientation, 
                                exclude_primitives);

  if (surface_hit.primitive == ID_NONE) {
    return {INFTY, ID_NONE};
  }

  if (exclude_primitives) {
    exclude_primitives->push_back(surface_hit.primitive);
  }

  return {surface_hit.distance, surface_hit.surface};
}

XDGRayHitBuffer CuBQLRayTracer::allocate_ray_hits(std::size_t count) const
{
  if (count == 0) {
    warning("Request to allocate 0 cuBQL XDG ray-hit buffer; returning empty buffer");
    return {};
  }

  auto* d_ray_hits = static_cast<XDGRayHit*>
    (omp_target_alloc(count * sizeof(XDGRayHit), context_.gpuID));

  if (!d_ray_hits) {
    fatal_error("Failed to allocate cuBQL XDG ray-hit buffer");
  }

  return {d_ray_hits, count, context_.gpuID};
}

void CuBQLRayTracer::free_ray_hits(XDGRayHitBuffer& ray_hits) const
{
  if (!ray_hits.data) {
    warning("Request to free empty cuBQL XDG ray-hit buffer; ignoring");
    return;
  }

  omp_target_free(ray_hits.data, ray_hits.device_id);
  ray_hits = {};
}

void
CuBQLRayTracer::ray_fire_batch(const XDGRayHitBuffer& ray_hits,
                               HitOrientation hit_orientation) const
{
  if (ray_hits.count == 0) return;

  if (!ray_hits.data) {
    fatal_error("Invalid cuBQL XDG ray-hit buffer");
  }

  if (!d_volume_to_group_) {
    fatal_error("cuBQL volume-group lookup table has not been uploaded");
  }

  intersect_surface_tree_batch(context_,
                               d_volume_to_group_,
                               ray_hits.data,
                               ray_hits.count,
                               hit_orientation);
}

std::pair<double, MeshID>
CuBQLRayTracer::closest(TreeID, const Position&)
{
  fatal_error("Closest queries not currently supported with cuBQL ray tracer");
  return {INFTY, ID_NONE};
}

bool CuBQLRayTracer::occluded(TreeID,
                              const Position&,
                              const Direction&,
                              double&) const
{
  fatal_error("Occlusion queries not currently supported with cuBQL ray tracer");
  return false;
}

} // namespace xdg
