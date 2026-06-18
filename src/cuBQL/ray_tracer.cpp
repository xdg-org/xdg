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
  if (d_volume_to_tlas_) {
    omp_target_free(d_volume_to_tlas_, context_.gpuID);
    d_volume_to_tlas_ = nullptr;
  }

  for (auto& [tree, tlas] : tree_to_volume_tlas_) {
    tlas.release();
  }

  for (auto& [surface, blas] : surface_to_blas_map_) {
    blas.release();
  }
}

void CuBQLRayTracer::init()
{
  upload_volume_to_tlas_table_();
  initialized_ = true;
}

void CuBQLRayTracer::upload_volume_to_tlas_table_()
{
  if (d_volume_to_tlas_) {
    omp_target_free(d_volume_to_tlas_, context_.gpuID);
    d_volume_to_tlas_ = nullptr;
  }

  if (volume_to_tlas_.empty()) {
    return;
  }

  d_volume_to_tlas_ = static_cast<CuBQLVolumeTLAS::DD*>
    (omp_target_alloc(volume_to_tlas_.size() * sizeof(CuBQLVolumeTLAS::DD), context_.gpuID));
  omp_target_memcpy(d_volume_to_tlas_,
                    volume_to_tlas_.data(),
                    volume_to_tlas_.size() * sizeof(CuBQLVolumeTLAS::DD),
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

CuBQLSurfaceBLAS
CuBQLRayTracer::register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                                  MeshID surface_id,
                                  double bounding_box_bump)
{
  auto num_faces = mesh_manager->num_surface_faces(surface_id);
  auto vertices = mesh_manager->get_surface_vertices(surface_id);
  auto indices = mesh_manager->get_surface_connectivity(surface_id);

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

  std::vector<MeshID> h_primitive_refs = mesh_manager->get_surface_faces(surface_id);

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

  auto* d_primitive_refs = static_cast<MeshID*>
    (omp_target_alloc(h_primitive_refs.size() * sizeof(MeshID), context_.gpuID));
  omp_target_memcpy(d_primitive_refs,
                    h_primitive_refs.data(),
                    h_primitive_refs.size() * sizeof(MeshID),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  auto* d_aabbs = static_cast<cuBQL::box3f*>
    (omp_target_alloc(h_indices.size() * sizeof(cuBQL::box3f), context_.gpuID));
  const auto num_primitives = static_cast<uint32_t>(h_indices.size());

  // TODO - Abstract this out into its own bounding_box creation function
  #pragma omp target device(context_.gpuID) is_device_ptr(d_vertices, d_indices, d_aabbs) \
    firstprivate(bounding_box_bump)
  #pragma omp teams distribute parallel for
  for (uint32_t primID = 0; primID < num_primitives; ++primID) {
    cuBQL::vec3i indices = d_indices[primID];

    cuBQL::vec3d A = d_vertices[indices.x];
    cuBQL::vec3d B = d_vertices[indices.y];
    cuBQL::vec3d C = d_vertices[indices.z];

    cuBQL::box3d aabb;
    aabb.extend(A);
    aabb.extend(B);
    aabb.extend(C);

    const cuBQL::vec3d bump(bounding_box_bump);
    aabb.lower = aabb.lower - bump;
    aabb.upper = aabb.upper + bump;

    d_aabbs[primID] = cuBQL::box3f(aabb);
  }

  cuBQL::BuildConfig blasBuildParams;
  // TODO - Try setting leaf params to 1 to see what it does
  // Check what default is for CUDA 
  cuBQL::bvh3f bvh;
  cuBQL::build_omp_target(bvh, d_aabbs, num_faces, blasBuildParams, context_.gpuID);

  omp_target_free(d_aabbs, context_.gpuID);

  CuBQLSurfaceMesh surface_mesh;
  surface_mesh.surface_id = surface_id;
  surface_mesh.d_vertices = d_vertices;
  surface_mesh.d_indices = d_indices;
  surface_mesh.d_primitive_refs = d_primitive_refs;
  surface_mesh.num_vertices = h_vertices.size();
  surface_mesh.num_triangles = num_faces;
  surface_mesh.gpu_id = context_.gpuID;

  CuBQLSurfaceBLAS surface_blas;
  surface_blas.bvh = bvh;
  surface_blas.mesh = surface_mesh;
  surface_blas.num_prims = num_faces;
  surface_blas.gpu_id = context_.gpuID;

  return surface_blas;
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
  std::vector<cuBQL::box3f> h_tlas_boxes;
  std::vector<CuBQLVolumeTLAS::SurfaceInstanceDD> h_surface_instances;
  h_tlas_boxes.reserve(volume_surfaces.size());
  h_surface_instances.reserve(volume_surfaces.size());

  for (const auto &surf : volume_surfaces) {
    auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);
    const double max_parent_bbox_bump = std::max(bounding_box_bump(mesh_manager, forward_parent),
                                                bounding_box_bump(mesh_manager, reverse_parent));

    if (!surface_to_blas_map_.count(surf)) {
      surface_to_blas_map_[surf] = register_surface(mesh_manager, surf, max_parent_bbox_bump);
    }

    CuBQLSurfaceBLAS& surface_blas = surface_to_blas_map_.at(surf);

    // Store BLAS bounding boxes to build TLAS
    const auto surface_bounding_box = mesh_manager->surface_bounding_box(surf);
    cuBQL::box3d surface_bounds_dp;
    surface_bounds_dp.lower = cuBQL::vec3d(surface_bounding_box.min_x,
                                           surface_bounding_box.min_y,
                                           surface_bounding_box.min_z);
    surface_bounds_dp.upper = cuBQL::vec3d(surface_bounding_box.max_x,
                                           surface_bounding_box.max_y,
                                           surface_bounding_box.max_z);

    const cuBQL::vec3d bump(max_parent_bbox_bump);
    surface_bounds_dp.lower = surface_bounds_dp.lower - bump;
    surface_bounds_dp.upper = surface_bounds_dp.upper + bump;
    cuBQL::box3f surface_bounds(surface_bounds_dp);

    CuBQLVolumeTLAS::SurfaceInstanceDD surface_instance;
    surface_instance.surface_blas = surface_blas.get_device_data();

    // Sense setting for each surface instance in the TLAS
    if (volume_id == forward_parent) {
      surface_instance.reverse_sense = false;
    } else if (volume_id == reverse_parent) {
      surface_instance.reverse_sense = true;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surf);
    }

    h_tlas_boxes.push_back(surface_bounds);
    h_surface_instances.push_back(surface_instance);
  }

  if (h_surface_instances.empty()) {
    fatal_error("Volume {} has no surfaces; cannot build cuBQL surface tree", volume_id);
  }

  auto* d_tlas_boxes = static_cast<cuBQL::box3f*>
    (omp_target_alloc(h_tlas_boxes.size() * sizeof(cuBQL::box3f), context_.gpuID));
  omp_target_memcpy(d_tlas_boxes,
                    h_tlas_boxes.data(),
                    h_tlas_boxes.size() * sizeof(cuBQL::box3f),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  auto* d_surface_instances = static_cast<CuBQLVolumeTLAS::SurfaceInstanceDD*>
    (omp_target_alloc(h_surface_instances.size() * sizeof(CuBQLVolumeTLAS::SurfaceInstanceDD), context_.gpuID));
  omp_target_memcpy(d_surface_instances,
                    h_surface_instances.data(),
                    h_surface_instances.size() * sizeof(CuBQLVolumeTLAS::SurfaceInstanceDD),
                    0,
                    0,
                    context_.gpuID,
                    context_.hostID);

  cuBQL::BuildConfig tlasBuildParams;
  tlasBuildParams.makeLeafThreshold = 1;
  tlasBuildParams.maxAllowedLeafSize = 1;

  CuBQLVolumeTLAS volume_tlas;
  volume_tlas.volume_id = volume_id; // store meshid in the TLAS object for easier mapping between the two
  volume_tlas.num_surface_instances = static_cast<uint32_t>(h_surface_instances.size());
  volume_tlas.gpu_id = context_.gpuID;
  volume_tlas.d_surface_instances = d_surface_instances;
  cuBQL::build_omp_target(volume_tlas.bvh,
                          d_tlas_boxes,
                          volume_tlas.num_surface_instances,
                          tlasBuildParams,
                          context_.gpuID);

  omp_target_free(d_tlas_boxes, context_.gpuID);

  // Still required for lifetime and scalar calls which need to resolve TreeID->volume_tlas on CPU side.
  auto result = tree_to_volume_tlas_.emplace(tree, std::move(volume_tlas));
  auto it = result.first;

  // Keep a dense host-side MeshID -> TLAS device-data table for prepared queries.
  // The TLAS object in tree_to_volume_tlas_ owns the device allocations; this table
  // only stores lightweight DD views indexed by volume ID. Upload to device once in
  // init(), unless a volume is registered after initialization.

  const auto volume_index = static_cast<size_t>(volume_id);
  if (volume_index >= volume_to_tlas_.size()) {
    volume_to_tlas_.resize(volume_index + 1);
  }
  volume_to_tlas_[volume_index] = it->second.get_device_data();

  if (initialized_) {
    upload_volume_to_tlas_table_();
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
  const CuBQLVolumeTLAS& volume_tlas = tree_to_volume_tlas_.at(tree);

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
  intersect_surface_tree_scalar(context, volume_tlas, ray, surface_hit, HitOrientation::ANY, exclude_primitives);

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
  const CuBQLVolumeTLAS& volume_tlas = tree_to_volume_tlas_.at(tree);

  CuBQLRay ray;
  ray.origin = cuBQL::vec3d(origin.x, origin.y, origin.z);
  ray.direction = cuBQL::vec3d(direction.x, direction.y, direction.z);
  ray.tMin = 0.0;
  ray.tMax = tmax;

  CuBQLSurfaceHit surface_hit;

  // TODO - Maybe we can come up with a better name for this
  intersect_surface_tree_scalar(context, volume_tlas, ray, surface_hit, hitOrientation, exclude_primitives);

  if (surface_hit.primitive == ID_NONE) {
    return {INFTY, ID_NONE};
  }

  if (exclude_primitives) {
    exclude_primitives->push_back(surface_hit.primitive);
  }

  return {surface_hit.distance, surface_hit.surface};
}

void
CuBQLRayTracer::ray_fire_batch(const CuBQLRay* d_rays,
                               CuBQLSurfaceHit* d_hits,
                               std::size_t num_rays,
                               HitOrientation orientation)
{
  if (num_rays == 0) return;

  if (!d_volume_to_tlas_) {
    fatal_error("cuBQL volume TLAS lookup table has not been uploaded");
  }

  intersect_surface_tree_batch(context_,
                               d_volume_to_tlas_,
                               d_rays,
                               d_hits,
                               num_rays,
                               orientation);
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
