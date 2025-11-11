#include "xdg/gprt/ray_tracer.h"
#include "gprt/gprt.h"

namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  gprtRequestRayTypeCount(numRayTypes_); // Set the number of shaders which can be set to the same geometry
  context_ = gprtContextCreate();
  module_ = gprtModuleCreate(context_, dbl_deviceCode);

  rayHitBuffers_.capacity = 1; // Preallocate space for 1 ray
  rayHitBuffers_.ray = gprtDeviceBufferCreate<dblRay>(context_, rayHitBuffers_.capacity);
  rayHitBuffers_.hit = gprtDeviceBufferCreate<dblHit>(context_, rayHitBuffers_.capacity);
  rayHitBuffers_.devRayAddr = gprtBufferGetDevicePointer(rayHitBuffers_.ray);
  rayHitBuffers_.devHitAddr = gprtBufferGetDevicePointer(rayHitBuffers_.hit);

  excludePrimitivesBuffer_ = gprtDeviceBufferCreate<int32_t>(context_); // initialise buffer of size 1

  setup_shaders();

  
  // Bind the buffers to the RayGenData structure
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGenPrograms_.at(RayGenType::RAY_FIRE));
  rayGenData->ray = gprtBufferGetDevicePointer(rayHitBuffers_.ray);
  rayGenData->hit = gprtBufferGetDevicePointer(rayHitBuffers_.hit);

  // Bind the buffers to the RayGenData structure
  dblRayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPrograms_.at(RayGenType::POINT_IN_VOLUME));
  rayGenPIVData->ray = gprtBufferGetDevicePointer(rayHitBuffers_.ray);
  rayGenPIVData->hit = gprtBufferGetDevicePointer(rayHitBuffers_.hit);

  // Set up build parameters for acceleration structures
  buildParams_.buildMode = GPRT_BUILD_MODE_FAST_BUILD_NO_UPDATE;
}

GPRTRayTracer::~GPRTRayTracer()
{
  // Ensure all GPU operations are complete before destroying resources
  gprtGraphicsSynchronize(context_); 
  gprtComputeSynchronize(context_);


  // Destroy TLAS structures
  for (const auto& [tree, accel] : surface_volume_tree_to_accel_map) {
    gprtAccelDestroy(accel);
  }

  // Destroy BLAS structures
  for (const auto& blas : blas_handles_) {
    gprtAccelDestroy(blas);
  }

  // Destroy Geoms and Types
  for (const auto& [surf, geom] : surface_to_geometry_map_) {
    gprtGeomDestroy(geom);
  }
  gprtGeomTypeDestroy(trianglesGeomType_);

  // Destroy Buffers
  gprtBufferDestroy(rayHitBuffers_.ray);
  gprtBufferDestroy(rayHitBuffers_.hit);
  gprtBufferDestroy(excludePrimitivesBuffer_);

  // Destroy module and context
  gprtModuleDestroy(module_);
  gprtContextDestroy(context_);
}

void GPRTRayTracer::setup_shaders()
{
  // Set up ray generation and miss programs
  rayGenPrograms_[RayGenType::RAY_FIRE] = gprtRayGenCreate<dblRayGenData>(context_, module_, "ray_fire");
  rayGenPrograms_[RayGenType::POINT_IN_VOLUME] = gprtRayGenCreate<dblRayGenData>(context_, module_, "point_in_volume");
  // TODO: Add Occluded and closest raygen entry points

  missProgram_ = gprtMissCreate<void>(context_, module_, "ray_fire_miss");
  aabbPopulationProgram_ = gprtComputeCreate<DPTriangleGeomData>(context_, module_, "populate_aabbs");

  // Create a "triangle" geometry type and set its closest-hit program
  trianglesGeomType_ = gprtGeomTypeCreate<DPTriangleGeomData>(context_, GPRT_AABBS);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType_, 0, module_, "ray_fire_hit"); // closesthit for ray queries
  gprtGeomTypeSetIntersectionProg(trianglesGeomType_, 0, module_, "DPTrianglePluckerIntersection"); // set intersection program for double precision rays
}

void GPRTRayTracer::init() 
{
  // Build the shader binding table (SBT) after all shader programs and acceleration structures are set up
  gprtBuildShaderBindingTable(context_, GPRT_SBT_ALL);
  // Note that should we need to update any shaders or acceleration structures, we must rebuild the SBT  
}

std::pair<TreeID,TreeID>
GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume_id)
{
  // set up ray tracing tree for boundary faces of the volume
  TreeID faces_tree = create_surface_tree(mesh_manager, volume_id);
  // set up point location tree for any volumetric elements. TODO - currently not supported with GPRT
  TreeID element_tree = create_element_tree(mesh_manager, volume_id); 
  return {faces_tree, element_tree}; // return TREE_NONE for element tree until implmemented
}

SurfaceTreeID
GPRTRayTracer::create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume_id)
{
  SurfaceTreeID tree = next_surface_tree_id();
  surface_trees_.push_back(tree);
  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> surfaceBlasInstances; // BLAS for each (surface) geometry in this volume

  for (const auto &surf : volume_surfaces) {
    auto num_faces = mesh_manager->num_surface_faces(surf);

    // get the sense of this surface with respect to the volume
    Sense triangle_sense {Sense::UNSET};
    auto surf_to_vol_senses = mesh_manager->get_parent_volumes(surf);
    if (volume_id == surf_to_vol_senses.first) triangle_sense = Sense::FORWARD;
    else if (volume_id == surf_to_vol_senses.second) triangle_sense = Sense::REVERSE;
    
    DPTriangleGeomData* geom_data = nullptr;
    auto triangleGeom = gprtGeomCreate<DPTriangleGeomData>(context_, trianglesGeomType_);
    geom_data = gprtGeomGetParameters(triangleGeom); // pointer to assign data to

    // Get storage for vertices
    auto [vertices, indices] = mesh_manager->get_surface_mesh(surf);
    std::vector<double3> dbl3Vertices;
    dbl3Vertices.reserve(vertices.size());    
    for (const auto &vertex : vertices) {
      dbl3Vertices.push_back({vertex.x, vertex.y, vertex.z});
    }

    // Get storage for indices
    std::vector<uint3> ui3Indices;
    ui3Indices.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
      ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
    }

    // Get storage for normals
    std::vector<double3> normals;
    std::vector<GPRTPrimitiveRef> primitive_refs;
    primitive_refs.reserve(num_faces);
    normals.reserve(num_faces);
    for (const auto &face : mesh_manager->get_surface_faces(surf)) {
      auto norm = mesh_manager->face_normal(face);
      normals.push_back({norm.x, norm.y, norm.z});
      GPRTPrimitiveRef prim_ref;
      prim_ref.id = face;
      primitive_refs.push_back(prim_ref);
    }

    auto vertex_buffer = gprtDeviceBufferCreate<double3>(context_, dbl3Vertices.size(), dbl3Vertices.data());
    auto aabb_buffer = gprtDeviceBufferCreate<float3>(context_, 2*num_faces, 0); // AABBs for each triangle
    gprtAABBsSetPositions(triangleGeom, aabb_buffer, num_faces, 2*sizeof(float3), 0);
    auto connectivity_buffer = gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data());
    auto normal_buffer = gprtDeviceBufferCreate<double3>(context_, num_faces, normals.data()); 
    auto primitive_refs_buffer = gprtDeviceBufferCreate<GPRTPrimitiveRef>(context_, num_faces, primitive_refs.data()); // Buffer for primitive sense

    geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffer);
    geom_data->index = gprtBufferGetDevicePointer(connectivity_buffer);
    geom_data->aabbs = gprtBufferGetDevicePointer(aabb_buffer);
    geom_data->ray = gprtBufferGetDevicePointer(rayHitBuffers_.ray);
    geom_data->surf_id = surf;
    geom_data->normals = gprtBufferGetDevicePointer(normal_buffer);
    geom_data->primitive_refs = gprtBufferGetDevicePointer(primitive_refs_buffer);
    geom_data->num_faces = num_faces;
    
    gprtComputeLaunch(aabbPopulationProgram_, {num_faces, 1, 1}, {1, 1, 1}, *geom_data);

    GPRTAccel blas = gprtAABBAccelCreate(context_, triangleGeom, buildParams_.buildMode);

    gprtAccelBuild(context_, blas, buildParams_);

    gprt::Instance instance;
    instance = gprtAccelGetInstance(blas); // create instance of BLAS to be added to TLAS
    instance.mask = 0xff; // mask can be used to filter instances during ray traversal. 0xff ensures no filtering

    // Store in maps
    surface_to_geometry_map_[surf] = triangleGeom;

    geom_data = gprtGeomGetParameters(triangleGeom);
    instance = gprtAccelGetInstance(blas);
    instance.mask = 0xff;
    surfaceBlasInstances.push_back(instance);
    globalBlasInstances_.push_back(instance);
    
    // Always update per-volume info
    auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);
    if (volume_id == forward_parent) {
      geom_data->forward_vol = forward_parent;
      geom_data->forward_tree = tree;
    } else if (volume_id == reverse_parent) {
      geom_data->reverse_vol = reverse_parent;
      geom_data->reverse_tree = tree;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surf);
    }
  }
  gprtComputeSynchronize(context_); // Ensure all GPU operations are complete before accessing results
  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances in this volume
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, surfaceBlasInstances.size(), surfaceBlasInstances.data());
  GPRTAccel volume_tlas = gprtInstanceAccelCreate(context_, surfaceBlasInstances.size(), instanceBuffer);
  gprtAccelBuild(context_, volume_tlas, buildParams_);
  surface_volume_tree_to_accel_map[tree] = volume_tlas;
  
  return tree;
}

ElementTreeID
GPRTRayTracer::create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume_id)
{
  warning("Element trees not currently supported with GPRT ray tracer");
  return TREE_NONE;
};

bool GPRTRayTracer::point_in_volume(SurfaceTreeID tree, 
                                    const Position& point,
                                    const Direction* direction,
                                    const std::vector<MeshID>* exclude_primitives) const
{
  GPRTAccel volume = surface_volume_tree_to_accel_map.at(tree);
  auto rayGen = rayGenPrograms_.at(RayGenType::POINT_IN_VOLUME);
  dblRayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGen);

  const Direction defaultDir = Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};

  // Use provided direction or if Direction == nulptr use default direction
  Direction directionUsed = (direction != nullptr) ? Direction{direction->x, direction->y, direction->z} 
                            : defaultDir;

  // Catch directions with zero length
  const double l2 = directionUsed.x*directionUsed.x
                  + directionUsed.y*directionUsed.y
                  + directionUsed.z*directionUsed.z;
  if (l2 == 0.0) directionUsed = defaultDir;

  gprtBufferMap(rayHitBuffers_.ray); // Update the ray input buffer
  dblRay* ray = gprtBufferGetHostPointer(rayHitBuffers_.ray);
  ray[0].volume_accel = gprtAccelGetDeviceAddress(volume); 
  ray[0].origin = {point.x, point.y, point.z};
  ray[0].direction = {directionUsed.x, directionUsed.y, directionUsed.z};
  ray[0].tMax = INFTY; // Set a large distance limit
  ray[0].tMin = 0.0;
  ray[0].volume_tree = tree; // Set the TreeID of the volume being queried
  ray[0].hitOrientation = HitOrientation::ANY; // No orientation culling for point-in-volume check

  if (exclude_primitives) {
    if (!exclude_primitives->empty()) gprtBufferResize(context_, excludePrimitivesBuffer_, exclude_primitives->size(), false);
    gprtBufferMap(excludePrimitivesBuffer_);
    std::copy(exclude_primitives->begin(), exclude_primitives->end(), gprtBufferGetHostPointer(excludePrimitivesBuffer_));
    gprtBufferUnmap(excludePrimitivesBuffer_);

    ray[0].exclude_primitives = gprtBufferGetDevicePointer(excludePrimitivesBuffer_);
    ray[0].exclude_count = exclude_primitives->size();
  } 
  else {
    // If no primitives are excluded, set the pointer to null and count to 0
    ray[0].exclude_primitives = nullptr;
    ray[0].exclude_count = 0;
  }
  gprtBufferUnmap(rayHitBuffers_.ray); // required to sync buffer back on GPU?

  gprtRayGenLaunch1D(context_, rayGen, 1); // Launch raygen shader (entry point to RT pipeline)
  gprtGraphicsSynchronize(context_); // Ensure all GPU operations are complete before returning control flow to CPU

  // Retrieve the hit from the dblHit buffer
  gprtBufferMap(rayHitBuffers_.hit);
  dblHit* hit = gprtBufferGetHostPointer(rayHitBuffers_.hit);
  auto surface = hit[0].surf_id;
  auto piv = hit[0].piv; // Point in volume check result
  gprtBufferUnmap(rayHitBuffers_.hit); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  // if ray hit nothing, the point is outside volume
  if (surface == ID_NONE) return false;

  return piv;
}


// This will launch the rays and run our shaders in the ray tracing pipeline
// miss shader returns dist = 0.0 and elementID = -1
// closest hit shader returns dist = distance to hit and elementID = triangle ID
std::pair<double, MeshID> GPRTRayTracer::ray_fire(SurfaceTreeID tree,
                                                  const Position& origin,
                                                  const Direction& direction,
                                                  double dist_limit,
                                                  HitOrientation orientation,
                                                  std::vector<MeshID>* const exclude_primitives) 
{
  GPRTAccel volume = surface_volume_tree_to_accel_map.at(tree);
  auto rayGen = rayGenPrograms_.at(RayGenType::RAY_FIRE);
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
  
  gprtBufferMap(rayHitBuffers_.ray); // Update the ray input buffer
  dblRay* ray = gprtBufferGetHostPointer(rayHitBuffers_.ray);
  ray[0].volume_accel = gprtAccelGetDeviceAddress(volume);
  ray[0].origin = {origin.x, origin.y, origin.z};
  ray[0].direction = {direction.x, direction.y, direction.z};
  ray[0].tMax = dist_limit;
  ray[0].tMin = 0.0;
  ray[0].hitOrientation = orientation; // Set orientation for the ray
  ray[0].volume_tree = tree; // Set the TreeID of the volume being queried

  if (exclude_primitives) {
    if (!exclude_primitives->empty()) gprtBufferResize(context_, excludePrimitivesBuffer_, exclude_primitives->size(), false);
    gprtBufferMap(excludePrimitivesBuffer_);
    std::copy(exclude_primitives->begin(), exclude_primitives->end(), gprtBufferGetHostPointer(excludePrimitivesBuffer_));
    gprtBufferUnmap(excludePrimitivesBuffer_);

    ray[0].exclude_primitives = gprtBufferGetDevicePointer(excludePrimitivesBuffer_);
    ray[0].exclude_count = exclude_primitives->size();
  } 
  else {
    // If no primitives are excluded, set the pointer to null and count to 0
    ray[0].exclude_primitives = nullptr;
    ray[0].exclude_count = 0;
  }
  gprtBufferUnmap(rayHitBuffers_.ray); // required to sync buffer back on GPU?
  
  gprtRayGenLaunch1D(context_, rayGen, 1); // Launch raygen shader (entry point to RT pipeline)
  gprtGraphicsSynchronize(context_); // Ensure all GPU operations are complete before returning control flow to CPU
                                                  
  // Retrieve the hit from the dblHit buffer
  gprtBufferMap(rayHitBuffers_.hit);
  dblHit* hit = gprtBufferGetHostPointer(rayHitBuffers_.hit);
  auto distance = hit[0].distance;
  auto surface = hit[0].surf_id;
  auto primitive_id = hit[0].primitive_id;
  gprtBufferUnmap(rayHitBuffers_.hit); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  if (surface == ID_NONE)
    return {INFTY, ID_NONE};
  else
    if (exclude_primitives) exclude_primitives->push_back(primitive_id);
  return {distance, surface};
}

void GPRTRayTracer::point_in_volume(TreeID tree,
                                          const Position* points,
                                          const Direction* directions, // [num_points] array of Direction pointers
                                          const size_t num_points,
                                          uint8_t* results,
                                          const uint8_t* has_dir,
                                          std::vector<MeshID>* exclude_primitives)
{
  if (num_points == 0) return; // no work to do. Early exit

  GPRTAccel volume = surface_volume_tree_to_accel_map.at(tree);
  auto rayGen = rayGenPrograms_.at(RayGenType::POINT_IN_VOLUME);
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
  check_ray_buffer_capacity(num_points);

  // TODO - handle exclude_primitives for batch version

  // Set a default direction to be used if no direction is provided
  const Direction defaultDir = Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};

  // Map the region start
  gprtBufferMap(rayHitBuffers_.ray); 
  dblRay* ray = gprtBufferGetHostPointer(rayHitBuffers_.ray);
  const auto volumeAddr = gprtAccelGetDeviceAddress(volume);
  for (size_t i = 0; i < num_points; ++i) {
    Direction directionUsed =
        (!directions || (has_dir && !has_dir[i])) ? defaultDir : directions[i];

    // Catch directions with zero length
    const double l2 = directionUsed.x*directionUsed.x
                    + directionUsed.y*directionUsed.y
                    + directionUsed.z*directionUsed.z;
    if (l2 == 0.0) directionUsed = defaultDir;

    ray[i].volume_accel = volumeAddr;
    ray[i].origin = {points[i].x, points[i].y, points[i].z};
    ray[i].direction = {directionUsed.x, directionUsed.y, directionUsed.z};
    ray[i].tMax = INFTY; // Set a large distance limit
    ray[i].tMin = 0.0;
    ray[i].volume_tree = tree; // Set the TreeID of the volume being queried
    ray[i].hitOrientation = HitOrientation::ANY; // No orientation culling for point-in-volume check
    ray[i].exclude_primitives = nullptr; // Not currently supported in batch version
  }

  gprtBufferUnmap(rayHitBuffers_.ray); // required to sync buffer back on GPU?
  
  // rebuild SBT geom and raygen only
  gprtBuildShaderBindingTable(context_, static_cast<GPRTBuildSBTFlags>(GPRT_SBT_GEOM | GPRT_SBT_RAYGEN));

  gprtRayGenLaunch1D(context_, rayGen, num_points);
  gprtGraphicsSynchronize(context_);

  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayHitBuffers_.hit);
  dblHit* hit = gprtBufferGetHostPointer(rayHitBuffers_.hit);
  for (size_t i = 0; i < num_points; ++i) {
    auto piv = hit[i].piv; // Point in volume check result
    results[i] = static_cast<uint8_t>(piv);
  }
  gprtBufferUnmap(rayHitBuffers_.hit); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  return;
}
  
// Array version of ray_fire
void GPRTRayTracer::ray_fire(TreeID tree,
                             const Position* origins,
                             const Direction* directions,
                             const size_t num_rays,
                             double* hitDistances,
                             MeshID* surfaceIDs,
                             const double dist_limit,
                             HitOrientation orientation,
                             std::vector<MeshID>* const exclude_primitives)
{
  if (num_rays == 0) return; // no work to do. Early exit

  GPRTAccel volume = surface_volume_tree_to_accel_map.at(tree);
  auto rayGen = rayGenPrograms_.at(RayGenType::RAY_FIRE);
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
  check_ray_buffer_capacity(num_rays);
    
  gprtBufferMap(rayHitBuffers_.ray); 
  dblRay* ray = gprtBufferGetHostPointer(rayHitBuffers_.ray);
  for (size_t i = 0; i < num_rays; ++i) {
    const auto& origin = origins[i];
    const auto& direction = directions[i];

    ray[i].volume_accel = gprtAccelGetDeviceAddress(volume);
    ray[i].origin = {origin.x, origin.y, origin.z};
    ray[i].direction = {direction.x, direction.y, direction.z};
    ray[i].tMax = dist_limit;
    ray[i].tMin = 0.0;
    ray[i].hitOrientation = orientation; // Set orientation for the ray
    ray[i].volume_tree = tree; // Set the TreeID of the volume being queried
    ray[i].exclude_primitives = nullptr; // Not currently supported in batch version
  }

  gprtBufferUnmap(rayHitBuffers_.ray); // required to sync buffer back on GPU?
  
  // rebuild SBT geom and raygen only
  gprtBuildShaderBindingTable(context_, static_cast<GPRTBuildSBTFlags>(GPRT_SBT_GEOM | GPRT_SBT_RAYGEN));
  
  // Launch the ray generation shader with push constants and buffer bindings
  gprtRayGenLaunch1D(context_, rayGen, num_rays);
  gprtGraphicsSynchronize(context_);
                                                  
  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayHitBuffers_.hit);
  dblHit* hit = gprtBufferGetHostPointer(rayHitBuffers_.hit);
  // populate the result arrays
  for (size_t i = 0; i < num_rays; ++i) {
    const MeshID surfaceHit = hit[i].surf_id;
    if (surfaceHit == ID_NONE) {
      hitDistances[i] = INFTY;
      surfaceIDs[i] = ID_NONE;
    }
    else {
      hitDistances[i] = hit[i].distance;
      surfaceIDs[i] = surfaceHit;
      // TODO - handle exclude_primitives for batch version
    }
  }
  gprtBufferUnmap(rayHitBuffers_.hit); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device

  return;
}

void GPRTRayTracer::create_global_surface_tree()
{
  // Create a TLAS (Top-Level Acceleration Structure) for all the volumes
  auto globalBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, globalBlasInstances_.size(), globalBlasInstances_.data());
  GPRTAccel global_accel = gprtInstanceAccelCreate(context_, globalBlasInstances_.size(), globalBuffer);
  gprtAccelBuild(context_, global_accel, buildParams_);

  SurfaceTreeID tree = next_surface_tree_id();
  surface_trees_.push_back(tree);
  surface_volume_tree_to_accel_map[tree] = global_accel;  
  global_surface_tree_ = tree;
  global_surface_accel_ = global_accel; 
}

void GPRTRayTracer::check_ray_buffer_capacity(const size_t N)
{
  if (N <= rayHitBuffers_.capacity) return; // current capacity is sufficient

  // Resize buffers to accommodate N rays
  size_t newCapacity = std::max(N, rayHitBuffers_.capacity * 2); // double the capacity or set to N, whichever is larger

  gprtBufferResize(context_, rayHitBuffers_.ray, newCapacity, false);
  gprtBufferResize(context_, rayHitBuffers_.hit, newCapacity, false);
  rayHitBuffers_.capacity = newCapacity;

  // Since we have resized the ray buffers, we need to update the geom_data->rayIn pointers in all geometries too 
  for (auto const& [surf, geom] : surface_to_geometry_map_) {
    DPTriangleGeomData* geom_data = gprtGeomGetParameters(geom);
    geom_data->ray = gprtBufferGetDevicePointer(rayHitBuffers_.ray); 
  }

  // Update raygen data pointers
  for (auto const& [type, rayGen] : rayGenPrograms_) {
    dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
    rayGenData->ray = gprtBufferGetDevicePointer(rayHitBuffers_.ray);
    rayGenData->hit = gprtBufferGetDevicePointer(rayHitBuffers_.hit);
  }

  gprtBuildShaderBindingTable(context_, static_cast<GPRTBuildSBTFlags>(GPRT_SBT_GEOM | GPRT_SBT_RAYGEN));
}

} // namespace xdg
