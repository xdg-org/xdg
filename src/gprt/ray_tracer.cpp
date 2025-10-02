#include "xdg/gprt/ray_tracer.h"
#include "gprt/gprt.h"

namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  gprtRequestRayTypeCount(numRayTypes_); // Set the number of shaders which can be set to the same geometry
  context_ = gprtContextCreate();
  module_ = gprtModuleCreate(context_, dbl_deviceCode);

  numRays = 1; // Set the number of rays to be cast
  rayInputBuffer_ = gprtDeviceBufferCreate<dblRayInput>(context_, numRays);
  rayOutputBuffer_ = gprtDeviceBufferCreate<dblRayOutput>(context_, numRays); 
  excludePrimitivesBuffer_ = gprtDeviceBufferCreate<int32_t>(context_); // initialise buffer of size 1

  setup_shaders();

  // Bind the buffers to the RayGenData structure
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
  rayGenData->out = gprtBufferGetDevicePointer(rayOutputBuffer_);

  // Bind the buffers to the RayGenData structure
  dblRayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPointInVolProgram_);
  rayGenPIVData->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
  rayGenPIVData->out = gprtBufferGetDevicePointer(rayOutputBuffer_);

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
  gprtBufferDestroy(rayInputBuffer_);
  gprtBufferDestroy(rayOutputBuffer_);
  gprtBufferDestroy(excludePrimitivesBuffer_);

  // Destroy module and context
  gprtModuleDestroy(module_);
  gprtContextDestroy(context_);
}

void GPRTRayTracer::setup_shaders()
{
  // Set up ray generation and miss programs
  rayGenProgram_ = gprtRayGenCreate<dblRayGenData>(context_, module_, "ray_fire");
  rayGenPointInVolProgram_ = gprtRayGenCreate<dblRayGenData>(context_, module_, "point_in_volume");
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
    geom_data->rayIn = gprtBufferGetDevicePointer(rayInputBuffer_);
    geom_data->surf_id = surf;
    geom_data->normals = gprtBufferGetDevicePointer(normal_buffer);
    geom_data->primitive_refs = gprtBufferGetDevicePointer(primitive_refs_buffer);
    geom_data->num_faces = num_faces;
    
    gprtComputeLaunch(aabbPopulationProgram_, {num_faces, 1, 1}, {1, 1, 1}, *geom_data);
    gprtComputeSynchronize(context_); // Ensure all GPU operations are complete before accessing results

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
  dblRayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPointInVolProgram_);

  // Use provided direction or if Direction == nulptr use default direction
  Direction directionUsed = (direction != nullptr) ? Direction{direction->x, direction->y, direction->z} 
                            : Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};

  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer
  dblRayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
  rayInput[0].volume_accel = gprtAccelGetDeviceAddress(volume); 
  rayInput[0].origin = {point.x, point.y, point.z};
  rayInput[0].direction = {directionUsed.x, directionUsed.y, directionUsed.z};
  rayInput[0].tMax = INFTY; // Set a large distance limit
  rayInput[0].tMin = 0.0;
  rayInput[0].volume_tree = tree; // Set the TreeID of the volume being queried
  rayInput[0].hitOrientation = HitOrientation::ANY; // No orientation culling for point-in-volume check

  if (exclude_primitives) {
    if (!exclude_primitives->empty()) gprtBufferResize(context_, excludePrimitivesBuffer_, exclude_primitives->size(), false);
    gprtBufferMap(excludePrimitivesBuffer_);
    std::copy(exclude_primitives->begin(), exclude_primitives->end(), gprtBufferGetHostPointer(excludePrimitivesBuffer_));
    gprtBufferUnmap(excludePrimitivesBuffer_);

    rayInput[0].exclude_primitives = gprtBufferGetDevicePointer(excludePrimitivesBuffer_);
    rayInput[0].exclude_count = exclude_primitives->size();
  } 
  else {
    // If no primitives are excluded, set the pointer to null and count to 0
    rayInput[0].exclude_primitives = nullptr;
    rayInput[0].exclude_count = 0;
  }
  gprtBufferUnmap(rayInputBuffer_); // required to sync buffer back on GPU?

  gprtRayGenLaunch1D(context_, rayGenPointInVolProgram_, 1); // Launch raygen shader (entry point to RT pipeline)
  gprtGraphicsSynchronize(context_); // Ensure all GPU operations are complete before returning control flow to CPU

  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayOutputBuffer_);
  dblRayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
  auto surface = rayOutput[0].surf_id;
  auto piv = rayOutput[0].piv; // Point in volume check result
  gprtBufferUnmap(rayOutputBuffer_); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
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
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  
  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer
  dblRayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
  rayInput[0].volume_accel = gprtAccelGetDeviceAddress(volume);
  rayInput[0].origin = {origin.x, origin.y, origin.z};
  rayInput[0].direction = {direction.x, direction.y, direction.z};
  rayInput[0].tMax = dist_limit;
  rayInput[0].tMin = 0.0;
  rayInput[0].hitOrientation = orientation; // Set orientation for the ray
  rayInput[0].volume_tree = tree; // Set the TreeID of the volume being queried

  if (exclude_primitives) {
    if (!exclude_primitives->empty()) gprtBufferResize(context_, excludePrimitivesBuffer_, exclude_primitives->size(), false);
    gprtBufferMap(excludePrimitivesBuffer_);
    std::copy(exclude_primitives->begin(), exclude_primitives->end(), gprtBufferGetHostPointer(excludePrimitivesBuffer_));
    gprtBufferUnmap(excludePrimitivesBuffer_);

    rayInput[0].exclude_primitives = gprtBufferGetDevicePointer(excludePrimitivesBuffer_);
    rayInput[0].exclude_count = exclude_primitives->size();
  } 
  else {
    // If no primitives are excluded, set the pointer to null and count to 0
    rayInput[0].exclude_primitives = nullptr;
    rayInput[0].exclude_count = 0;
  }
  gprtBufferUnmap(rayInputBuffer_); // required to sync buffer back on GPU?
  
  gprtRayGenLaunch1D(context_, rayGenProgram_, 1); // Launch raygen shader (entry point to RT pipeline)
  gprtGraphicsSynchronize(context_); // Ensure all GPU operations are complete before returning control flow to CPU
                                                  
  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayOutputBuffer_);
  dblRayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
  auto distance = rayOutput[0].distance;
  auto surface = rayOutput[0].surf_id;
  auto primitive_id = rayOutput[0].primitive_id;
  gprtBufferUnmap(rayOutputBuffer_); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  if (surface == ID_NONE)
    return {INFTY, ID_NONE};
  else
    if (exclude_primitives) exclude_primitives->push_back(primitive_id);
  return {distance, surface};
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

} // namespace xdg