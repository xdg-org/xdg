#include "xdg/gprt/ray_tracer.h"
#include "gprt/gprt.h"



namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  fbSize = {1000,1000};
  gprtRequestWindow(fbSize.x, fbSize.y, "XDG::render_mesh()");
  gprtRequestRayTypeCount(numRayTypes_); // Set the number of shaders which can be set to the same geometry
  context_ = gprtContextCreate();
  module_ = gprtModuleCreate(context_, flt_deviceCode);
}

GPRTRayTracer::~GPRTRayTracer()
{
  gprtContextDestroy(context_);
}

void GPRTRayTracer::setup_shaders()
{
  // Set up ray generation and miss programs
  rayGenProgram_ = gprtRayGenCreate<RayGenData>(context_, module_, "ray_fire");
  rayGenPointInVolProgram_ = gprtRayGenCreate<RayGenData>(context_, module_, "point_in_volume");
  missProgram_ = gprtMissCreate<void>(context_, module_, "ray_fire_miss");

  // TODO - Multimap to hold each shader assocaited with each query?
}

void GPRTRayTracer::init()
{
  // TODO - Should we just allocate a large chunk in the buffers to start with so that we don't have to resize?
  numRays = 1; // Set the number of rays to be cast
  rayInputBuffer_ = gprtDeviceBufferCreate<RayInput>(context_, numRays);
  rayOutputBuffer_ = gprtDeviceBufferCreate<RayOutput>(context_, numRays); 
  excludePrimitivesBuffer_ = gprtDeviceBufferCreate<int32_t>(context_); // initialise buffer of size 1

  setup_shaders();

  // Bind the buffers to the RayGenData structure
  RayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
  rayGenData->out = gprtBufferGetDevicePointer(rayOutputBuffer_);

  // Bind the buffers to the RayGenData structure
  RayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPointInVolProgram_);
  rayGenPIVData->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
  rayGenPIVData->out = gprtBufferGetDevicePointer(rayOutputBuffer_);
  
}

TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);

  // Create a "triangle" geometry type and set its closest-hit program
  auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context_, GPRT_TRIANGLES);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module_, "ray_fire_hit"); // closesthit for ray queries

  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 1, module_, "render_hits"); // cloesthit for mesh rendering

  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> localBlasInstances; // BLAS for each (surface) geometry in this volume

  for (const auto &surf : volume_surfaces) {
    // Get surface mesh vertices and associated connectivities
    auto [vertices, indices] = mesh_manager->get_surface_mesh(surf);

    GPRTBufferOf<float3> vertex_buffer;
    GPRTBufferOf<uint3> connectivity_buffer;
    GPRTBufferOf<float3> normal_buffer;
    GPRTGeomOf<TrianglesGeomData> triangleGeom;

    // Convert vertices to float3 
    std::vector<float3> fl3Vertices;
    fl3Vertices.reserve(vertices.size());    
    for (const auto &vertex : vertices) {
      fl3Vertices.emplace_back(vertex.x, vertex.y, vertex.z);
    }

    // Convert connectivities/indices to uint3
    std::vector<uint3> ui3Indices;
    ui3Indices.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
      ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
    }

    // Compute face normals
    std::vector<float3> faceNormals;
    faceNormals.reserve(ui3Indices.size());
    for (const auto& tri : ui3Indices) {
      float3 v0 = fl3Vertices[tri.x];
      float3 v1 = fl3Vertices[tri.y];
      float3 v2 = fl3Vertices[tri.z];
      float3 normal = normalize(cross(v1 - v0, v2 - v0));
      faceNormals.push_back(normal);
    }

    // Create GPRT buffers and geometry data
    vertex_buffer = gprtDeviceBufferCreate<float3>(context_, fl3Vertices.size(), fl3Vertices.data());
    connectivity_buffer = gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data());
    normal_buffer = gprtDeviceBufferCreate<float3>(context_, faceNormals.size(), faceNormals.data());
    triangleGeom = gprtGeomCreate<TrianglesGeomData>(context_, trianglesGeomType);
    TrianglesGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
    geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffer);
    geom_data->index = gprtBufferGetDevicePointer(connectivity_buffer);
    geom_data->normals = gprtBufferGetDevicePointer(normal_buffer);
    geom_data->id = surf;
    auto [forward_parent, reverse_parent] = mesh_manager->get_parent_volumes(surf);
    geom_data->vols = {forward_parent, reverse_parent}; // Store both parent volumes
    geom_data->sense = static_cast<int>(mesh_manager->surface_sense(surf, volume_id)); // 0 for forward, 1 for reverse

    // Set vertices and indices for the triangle geometry
    gprtTrianglesSetVertices(triangleGeom, vertex_buffer, fl3Vertices.size());
    gprtTrianglesSetIndices(triangleGeom, connectivity_buffer, ui3Indices.size());

    // Create a BLAS for the triangle geometry
    GPRTAccel blas = gprtTriangleAccelCreate(context_, triangleGeom, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
    gprtAccelBuild(context_, blas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
    
    // Store the BLAS for concatenation into the TLAS
    localBlasInstances.push_back(gprtAccelGetInstance(blas));

  }

  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances in this volume
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, localBlasInstances.size(), localBlasInstances.data());
  GPRTAccel volume_tlas = gprtInstanceAccelCreate(context_, localBlasInstances.size(), instanceBuffer);
  gprtAccelBuild(context_, volume_tlas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
  tree_to_vol_accel_map[tree] = volume_tlas;

  // Store the local BLAS instances for this volume in global BLAS array
  for (const auto& instance : localBlasInstances) {
    globalBlasInstances_.push_back(instance);
  }
  return tree;
}

bool GPRTRayTracer::point_in_volume(TreeID tree, 
                                    const Position& point,
                                    const Direction* direction,
                                    const std::vector<MeshID>* exclude_primitives) const
{
  GPRTAccel volume = tree_to_vol_accel_map.at(tree);
  RayGenData* rayGenPIVData = gprtRayGenGetParameters(rayGenPointInVolProgram_);
  rayGenPIVData->world = gprtAccelGetDeviceAddress(volume);

  // Use provided direction or if Direction == nulptr use default direction
  Direction directionUsed = (direction != nullptr) ? Direction{direction->x, direction->y, direction->z} 
                            : Direction{1. / std::sqrt(2.0), 1. / std::sqrt(2.0), 0.0};

  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer

  RayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
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
  RayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
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
  RayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->world = gprtAccelGetDeviceAddress(volume);
  
  gprtBufferMap(rayInputBuffer_); // Update the ray input buffer

  RayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
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
  RayOutput* rayOutput = gprtBufferGetHostPointer(rayOutputBuffer_);
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


  create_world_tlas();

  // Set up ray generation and miss programs
  GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context_, module_, "render_mesh");
  GPRTMissOf<void> miss = gprtMissCreate<void>(context_, module_, "render_miss");

  // Place a reference to the TLAS in the ray generation kernel's parameters
  RayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
  rayGenData->world = gprtAccelGetDeviceAddress(world_);

  // Create the framebuffer
  GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context_, fbSize.x * fbSize.y);
  rayGenData->frameBuffer = gprtBufferGetDevicePointer(frameBuffer);

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
