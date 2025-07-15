#include "xdg/gprt/ray_tracer.h"
#include "gprt/gprt.h"



namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  fbSize = {1000,1000};
  // gprtRequestWindow(fbSize.x, fbSize.y, "XDG::render_mesh()");
  gprtRequestRayTypeCount(numRayTypes_); // Set the number of shaders which can be set to the same geometry
  context_ = gprtContextCreate();
  // module_ = gprtModuleCreate(context_, flt_deviceCode);
  module_ = gprtModuleCreate(context_, dbl_deviceCode);

  // TODO - Should we just allocate a large chunk in the buffers to start with so that we don't have to resize?
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
}

GPRTRayTracer::~GPRTRayTracer()
{
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
  // trianglesGeomType_ = gprtGeomTypeCreate<TrianglesGeomData>(context_, GPRT_TRIANGLES);
  trianglesGeomType_ = gprtGeomTypeCreate<DPTriangleGeomData>(context_, GPRT_AABBS);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType_, 0, module_, "ray_fire_hit"); // closesthit for ray queries
  gprtGeomTypeSetIntersectionProg(trianglesGeomType_, 0, module_, "DPTrianglePluckerIntersection"); // set intersection program for double precision rays

  // gprtGeomTypeSetClosestHitProg(trianglesGeomType_, 1, module_, "render_hits"); // cloesthit for mesh rendering
}

void GPRTRayTracer::init() {}

TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);
  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> surfaceBlasInstances; // BLAS for each (surface) geometry in this volume

  std::cout << "[register_volume] volume_id=" << volume_id << " surfaces=" << volume_surfaces.size() << std::endl;

  for (const auto &surf : volume_surfaces) {
    bool first_visit = !surface_to_geometry_map_.count(surf);
    auto triangleGeom = gprtGeomCreate<DPTriangleGeomData>(context_, trianglesGeomType_);
    gprt::Instance instance;
    auto num_faces = mesh_manager->num_surface_faces(surf);

    std::cout << "  Surface " << surf << (first_visit ? " (first visit)" : " (already registered)") << std::endl;

    // if (first_visit)
    // {
      auto [vertices, indices] = mesh_manager->get_surface_mesh(surf);
      std::vector<double3> dbl3Vertices;
      dbl3Vertices.reserve(vertices.size());    
      for (const auto &vertex : vertices) {
        dbl3Vertices.push_back({vertex.x, vertex.y, vertex.z});
      }

      std::vector<uint3> ui3Indices;
      ui3Indices.reserve(indices.size() / 3);
      for (size_t i = 0; i < indices.size(); i += 3) {
        ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
      }

      auto vertex_buffer = gprtDeviceBufferCreate<double3>(context_, dbl3Vertices.size(), dbl3Vertices.data());
      auto aabb_buffer = gprtDeviceBufferCreate<float3>(context_, 2*num_faces, 0); // AABBs for each triangle
      gprtAABBsSetPositions(triangleGeom, aabb_buffer, num_faces, 2*sizeof(float3), 0);
      auto connectivity_buffer = gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data());
      
      DPTriangleGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
      geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffer);
      geom_data->index = gprtBufferGetDevicePointer(connectivity_buffer);
      geom_data->aabbs = gprtBufferGetDevicePointer(aabb_buffer);
      geom_data->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
      geom_data->surf_id = surf;

      gprtComputeLaunch(aabbPopulationProgram_, {num_faces, 1, 1}, {1, 1, 1}, *geom_data);

      // gprtBufferMap(aabb_buffer);
      // float3* aabbs_host = gprtBufferGetHostPointer(aabb_buffer);
      // for (size_t i = 0; i < 2*mesh_manager->num_surface_faces(surf); i += 2) {
      //   float3 aabb_min = aabbs_host[i];
      //   float3 aabb_max = aabbs_host[i + 1];
      //   std::cout << "AABB " << i/2 << ": min(" << aabb_min.x << ", " << aabb_min.y << ", " << aabb_min.z
      //             << "), max(" << aabb_max.x << ", " << aabb_max.y << ", " << aabb_max.z << ")" << std::endl;
      // }
      // gprtBufferUnmap(aabb_buffer);

      GPRTAccel blas = gprtAABBAccelCreate(context_, triangleGeom, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
      gprtAccelBuild(context_, blas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);

      instance = gprtAccelGetInstance(blas);
      instance.mask = 0xff;

      // Store for lifetime management
      // vertex_buffers.push_back(vertex_buffer);
      // connectivity_buffers.push_back(connectivity_buffer);
      // allTrianglesGeom.push_back(triangleGeom);

      // Store in maps
      surface_to_geometry_map_[surf] = triangleGeom;
      surface_to_instance_map_[surf] = instance;
      globalBlasInstances_.push_back(instance);

      std::cout << "    globalBlasInstances_ size now: " << globalBlasInstances_.size() << std::endl;
    // }
    // else 
    // {
    //   triangleGeom = surface_to_geometry_map_.at(surf);
    //   instance = surface_to_instance_map_.at(surf);
    // }
    surfaceBlasInstances.push_back(instance);
    
    // Always update per-volume info
    // DPTriangleGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
    auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);

    if (volume_id == forward_parent) {
      geom_data->forward_vol = forward_parent;
    } else if (volume_id == reverse_parent) {
      geom_data->reverse_vol = reverse_parent;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surf);
    }
  }



  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances in this volume
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, surfaceBlasInstances.size(), surfaceBlasInstances.data());
  GPRTAccel volume_tlas = gprtInstanceAccelCreate(context_, surfaceBlasInstances.size(), instanceBuffer);
  gprtAccelBuild(context_, volume_tlas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
  tree_to_vol_accel_map[tree] = volume_tlas;

  return tree;
}

/* single precision version of register_volume
TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);
  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> surfaceBlasInstances; // BLAS for each (surface) geometry in this volume

  std::cout << "[register_volume] volume_id=" << volume_id << " surfaces=" << volume_surfaces.size() << std::endl;

  for (const auto &surf : volume_surfaces) {
    bool first_visit = !surface_to_geometry_map_.count(surf);
    GPRTGeomOf<TrianglesGeomData> triangleGeom;
    gprt::Instance instance;

    std::cout << "  Surface " << surf << (first_visit ? " (first visit)" : " (already registered)") << std::endl;

    if (first_visit)
    {
      auto [vertices, indices] = mesh_manager->get_surface_mesh(surf);

      std::vector<float3> fl3Vertices;
      fl3Vertices.reserve(vertices.size());    
      for (const auto &vertex : vertices) {
        fl3Vertices.emplace_back(vertex.x, vertex.y, vertex.z);
      }

      std::vector<uint3> ui3Indices;
      ui3Indices.reserve(indices.size() / 3);
      for (size_t i = 0; i < indices.size(); i += 3) {
        ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
      }

      std::cout << "    verts=" << fl3Vertices.size() << " tris=" << ui3Indices.size() << std::endl;
      std::cout << "    indices: ";
      for (const auto& tri : ui3Indices) {
        std::cout << "(" << tri.x << "," << tri.y << "," << tri.z << ") ";
      }
      std::cout << std::endl;

      auto vertex_buffer = gprtDeviceBufferCreate<float3>(context_, fl3Vertices.size(), fl3Vertices.data());
      auto connectivity_buffer = gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data());
      auto normal_buffer = gprtDeviceBufferCreate<float3>(context_, ui3Indices.size()); // dummy for now
      triangleGeom = gprtGeomCreate<TrianglesGeomData>(context_, trianglesGeomType_);

      TrianglesGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
      geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffer);
      geom_data->index = gprtBufferGetDevicePointer(connectivity_buffer);
      geom_data->normals = gprtBufferGetDevicePointer(normal_buffer);
      geom_data->id = surf;

      gprtTrianglesSetVertices(triangleGeom, vertex_buffer, fl3Vertices.size());
      gprtTrianglesSetIndices(triangleGeom, connectivity_buffer, ui3Indices.size());

      GPRTAccel blas = gprtTriangleAccelCreate(context_, triangleGeom, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
      gprtAccelBuild(context_, blas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);

      instance = gprtAccelGetInstance(blas);
      instance.mask = 0xff;

      // Store for lifetime management
      vertex_buffers.push_back(vertex_buffer);
      connectivity_buffers.push_back(connectivity_buffer);
      allTrianglesGeom.push_back(triangleGeom);

      // Store in maps
      surface_to_geometry_map_[surf] = triangleGeom;
      surface_to_instance_map_[surf] = instance;
      globalBlasInstances_.push_back(instance);

      std::cout << "    globalBlasInstances_ size now: " << globalBlasInstances_.size() << std::endl;
    }
    else 
    {
      triangleGeom = surface_to_geometry_map_.at(surf);
      instance = surface_to_instance_map_.at(surf);
    }
    surfaceBlasInstances.push_back(instance);

    // Always update per-volume info
    TrianglesGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
    auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);

    if (volume_id == forward_parent) {
      geom_data->forward_vol = forward_parent;
    } else if (volume_id == reverse_parent) {
      geom_data->reverse_vol = reverse_parent;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surf);
    }
  }


  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances in this volume
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, surfaceBlasInstances.size(), surfaceBlasInstances.data());
  GPRTAccel volume_tlas = gprtInstanceAccelCreate(context_, surfaceBlasInstances.size(), instanceBuffer);
  gprtAccelBuild(context_, volume_tlas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
  tree_to_vol_accel_map[tree] = volume_tlas;

  return tree;
}
*/


bool GPRTRayTracer::point_in_volume(TreeID tree, 
                                    const Position& point,
                                    const Direction* direction,
                                    const std::vector<MeshID>* exclude_primitives) const
{
  GPRTAccel volume = tree_to_vol_accel_map.at(tree);
  dblRayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPointInVolProgram_);
  rayGenPIVData->world = gprtAccelGetDeviceAddress(volume);

  // Use provided direction or if Direction == nulptr use default direction
  Direction directionUsed = (direction != nullptr) ? Direction{direction->x, direction->y, direction->z} 
                            : Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};

  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer

  dblRayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
  rayInput[0].origin = {point.x, point.y, point.z};
  rayInput[0].direction = {directionUsed.x, directionUsed.y, directionUsed.z};

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
  gprtBuildShaderBindingTable(context_, GPRT_SBT_ALL);

  gprtRayGenLaunch1D(context_, rayGenPointInVolProgram_, 1);

  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayOutputBuffer_);
  dblRayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
  auto surface = rayOutput[0].surf_id;
  Direction normal = {rayOutput[0].normal.x, rayOutput[0].normal.y, rayOutput[0].normal.z};
  gprtBufferUnmap(rayOutputBuffer_); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  // if ray hit nothing, the point is outside volume
  if (surface == XDG_GPRT_INVALID_GEOMETRY_ID) return false;

  // use the hit triangle normal to determine if the intersection is exiting or entering
  // TODO - Do this on GPU and return an int 1 or 0 to represent the bool?

  return directionUsed.dot(normal) > 0.0;
}


// This will launch the rays and run our shaders in the ray tracing pipeline
// miss shader returns dist = 0.0 and elementID = -1
// closest hit shader returns dist = distance to hit and elementID = triangle ID
std::pair<double, MeshID> GPRTRayTracer::ray_fire(TreeID scene,
                                                  const Position& origin,
                                                  const Direction& direction,
                                                  double dist_limit,
                                                  HitOrientation orientation,
                                                  std::vector<MeshID>* const exclude_primitives) 
{
  GPRTAccel volume = tree_to_vol_accel_map.at(scene);
  dblRayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->world = gprtAccelGetDeviceAddress(volume);
  
  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer

  dblRayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
  rayInput[0].origin = {origin.x, origin.y, origin.z};
  rayInput[0].direction = {direction.x, direction.y, direction.z};

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

  // pushconstants
  RayFirePushConstants pc; 

  pc.dist_limit = dist_limit;
  pc.orientation = static_cast<int>(orientation);
  
  gprtBuildShaderBindingTable(context_, GPRT_SBT_ALL);
  
  // Launch the ray generation shader with push constants and buffer bindings
  gprtRayGenLaunch1D(context_, rayGenProgram_, 1, pc);
                                                  
  // Retrieve the output from the ray output buffer
  gprtBufferMap(rayOutputBuffer_);
  dblRayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
  auto distance = rayOutput[0].distance;
  auto surface = rayOutput[0].surf_id;
  gprtBufferUnmap(rayOutputBuffer_); // required to sync buffer back on GPU? Maybe this second unmap isn't actually needed since we dont need to resyncrhonize after retrieving the data from device
  
  // if (surface == XDG_GPRT_INVALID_GEOMETRY_ID)
  //   return {INFTY, ID_NONE};
  // else
  //   if (exclude_primitives) exclude_primitives->push_back(surface);
  return {distance, surface};
}
                
void GPRTRayTracer::create_world_tlas()
{
  // Create a TLAS (Top-Level Acceleration Structure) for all the volumes
 
  std::cout << "[create_world_tlas] globalBlasInstances_ size: " << globalBlasInstances_.size() << std::endl;
  for (size_t i = 0; i < globalBlasInstances_.size(); ++i)
    std::cout << "  Instance " << i << " ptr: " << static_cast<const void*>(&globalBlasInstances_[i]) << std::endl;

  auto worldBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, globalBlasInstances_.size(), globalBlasInstances_.data());
  world_ = gprtInstanceAccelCreate(context_, globalBlasInstances_.size(), worldBuffer);
  gprtAccelBuild(context_, world_, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
}


//   return {0.0, 0};
// }

// void GPRTRayTracer::closest(TreeID scene,
//                             const Position& origin,
//                             double& dist,
//                             MeshID& triangle) {
//   // TODO: Closest hit logic with triangle
//   dist = -1.0;
//   triangle = -1;
// }

// void GPRTRayTracer::closest(TreeID scene,
//                             const Position& origin,
//                             double& dist) {
//   // TODO: Closest hit logic
//   dist = -1.0;
// }

// bool GPRTRayTracer::occluded(TreeID scene,
//                              const Position& origin,
//                              const Direction& direction,
//                              double& dist) const {
//   // TODO: Occlusion logic
//   dist = -1.0;
//   return false;
// }


// Methods for rendering mesh to framebuffer


void GPRTRayTracer::render_mesh(const std::shared_ptr<MeshManager> mesh_manager) 
{

  // Only render surfaces not included in the implicit complement
  // This has the side effect of seg faulting when all surfaces are in the implicit complement
  std::vector<gprt::Instance> explicitBlasInstances;
  MeshID complement_id = mesh_manager->implicit_complement();

  for (const auto& [surf, instance] : surface_to_instance_map_) {
      auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);
      if (forward_parent == complement_id || reverse_parent == complement_id)
          continue; // Skip complement surfaces
      explicitBlasInstances.push_back(instance);
  }

  // Now use explicitBlasInstances for TLAS creation
  auto worldBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, explicitBlasInstances.size(), explicitBlasInstances.data());
  world_ = gprtInstanceAccelCreate(context_, explicitBlasInstances.size(), worldBuffer);
  gprtAccelBuild(context_, world_, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
  // Set up ray generation and miss programs
  GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context_, module_, "render_mesh");
  GPRTMissOf<void> miss = gprtMissCreate<void>(context_, module_, "render_miss");

  // Place a reference to the TLAS in the ray generation kernel's parameters
  RayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
  rayGenData->world = gprtAccelGetDeviceAddress(world_);

  // Create the framebuffer
  GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context_, fbSize.x * fbSize.y);
  rayGenData->frameBuffer = gprtBufferGetDevicePointer(frameBuffer);

  std::cout << "[render_mesh] Building SBT with " << globalBlasInstances_.size() << " instances." << std::endl;
  gprtBuildShaderBindingTable(context_, GPRT_SBT_ALL);


  
  PushConstants pc;
      // Retrieve the bounding box of the TLAS
  auto worldBB = mesh_manager->world_bounding_box();
  auto tlasMin = float3(worldBB.min_x, worldBB.min_y, worldBB.min_z);
  auto tlasMax = float3(worldBB.max_x, worldBB.max_y, worldBB.max_z);

  float3 lookFrom = {1.5f, 6.f, -10.f};
  float3 lookAt = {1.5f, 1.5f, -1.5f};
  float3 lookUp = {0.f, -1.f, 0.f};
  float cosFovy = 0.66f;
  // Calculate the center of the bounding box
  float3 center = (tlasMin + tlasMax) * 0.5f;

  // Update camera parameters
  lookAt = center;

  // Calculate a suitable `lookFrom` position based on the bounding box size
  float3 boxSize = tlasMax - tlasMin;
  float distance = length(boxSize) * 1.0f; // Adjust the multiplier for desired zoom level
  lookFrom = center + float3(0.0f, 0.0f, -distance); // Place the camera behind the center

  // Set the up vector
  lookUp = float3(0.0f, 1.0f, 0.0f); // Y-axis up

  // Update push constants
  pc.scene_center = center;
  float maxDim = std::max(boxSize.x, std::max(boxSize.y, boxSize.z));
  pc.camera.radius = maxDim * 5.0f;
  pc.camera.pos = lookFrom;
  pc.camera.dir_00 = normalize(lookAt - lookFrom);
  float aspect = float(fbSize.x) / float(fbSize.y);
  pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
  pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
  pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
  pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;


  do {
    pc.time = float(gprtGetTime(context_));

    // Calls the GPU raygen kernel function
    gprtRayGenLaunch2D(context_, rayGen, fbSize.x, fbSize.y, pc);

    // If a window exists, presents the frame buffer here to that window
    gprtBufferPresent(context_, frameBuffer);
  }
  while (!gprtWindowShouldClose(context_));
}

} // namespace xdg
