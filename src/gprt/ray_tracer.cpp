#include "xdg/gprt/ray_tracer.h"
#include "gprt/gprt.h"



namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  // fbSize = {1000,1000};
  // gprtRequestWindow(fbSize.x, fbSize.y, "XDG::render_mesh()");
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
  missProgram_ = gprtMissCreate<void>(context_, module_, "ray_fire_miss");
}

void GPRTRayTracer::init()
{
  numRays = 1; // Set the number of rays to be cast
  rayInputBuffer_ = gprtDeviceBufferCreate<RayInput>(context_, numRays);
  rayOutputBuffer_ = gprtDeviceBufferCreate<RayOutput>(context_, numRays); 
  excludePrimitivesBuffer_ = gprtDeviceBufferCreate<int32_t>(context_); // initialise buffer of size 1

  setup_shaders();

  // Bind the buffers to the RayGenData structure
  RayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->ray = gprtBufferGetDevicePointer(rayInputBuffer_);
  rayGenData->out = gprtBufferGetDevicePointer(rayOutputBuffer_);
  

}

TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);

  // Create a "triangle" geometry type and set its closest-hit program
  auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context_, GPRT_TRIANGLES);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module_, "ray_fire_hit");
  // gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module_, "TriangleMesh");

  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> localBlasInstances; // BLAS for each (surface) geometry in this volume

  for (const auto &surf : volume_surfaces) {
    // Get surface mesh vertices and associated connectivities
    auto meshParams = mesh_manager->get_surface_mesh(surf);
    auto vertices = meshParams.first;
    auto indices = meshParams.second;

    GPRTBufferOf<float3> vertex_buffer;
    GPRTBufferOf<uint3> connectivity_buffer;
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

    // Create GPRT buffers and geometry data
    vertex_buffer = gprtDeviceBufferCreate<float3>(context_, fl3Vertices.size(), fl3Vertices.data());
    connectivity_buffer = gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data());
    triangleGeom = gprtGeomCreate<TrianglesGeomData>(context_, trianglesGeomType);
    TrianglesGeomData* geom_data = gprtGeomGetParameters(triangleGeom);
    geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffer);
    geom_data->index = gprtBufferGetDevicePointer(connectivity_buffer);
    geom_data->id = surf;
    geom_data->vols = { mesh_manager->get_parent_volumes(surf).first, mesh_manager->get_parent_volumes(surf).second };
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

// This will launch the rays and run our shaders in the ray tracing pipeline
// miss shader returns dist = 0.0 and elementID = -1
// closest hit shader returns dist = distance to hit and elementID = triangle ID
std::pair<double, MeshID> GPRTRayTracer::ray_fire(TreeID scene,
                                                  const Position& origin,
                                                  const Direction& direction,
                                                  double dist_limit,
                                                  HitOrientation orientation,
                                                  std::vector<MeshID>* const exclude_primitives) {
// Some logic for handling multiple rays
  // Allocate and fill rayBuffer with origins and directions...
  // std::vector<GPRTBufferOf<float3>> particleOriginBuffer;
  // std::vector<GPRTBufferOf<float3>> particleDirectionBuffer;
  // for (size_t i = 0; i < origins.size(); ++i) {
  //     particleOriginBuffer.push_back(gprtDeviceBufferCreate<float3>(context_, origins[i]));
  //     particleDirectionBuffer.push_back(gprtDeviceBufferCreate<float3>(context_, directions[i]));
  // }
  // Need to think about handling single vs double precision floating points

  // if (exclude_primitives != nullptr) rayhit.ray.exclude_primitives = exclude_primitives;
  GPRTAccel volume = tree_to_vol_accel_map.at(scene);

  // New: Here, we place a reference to our TLAS in the ray generation
  // kernel's parameters, so that we can access that tree when
  // we go to trace our rays.

  RayGenData* rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->world = gprtAccelGetDeviceAddress(volume);
  

  // Update the ray input buffer
  gprtBufferMap(rayInputBuffer_);
  RayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
  rayInput[0].origin = {origin.x, origin.y, origin.z};
  rayInput[0].direction = {direction.x, direction.y, direction.z};
  gprtBufferUnmap(rayInputBuffer_); // required to sync buffer back on GPU?

  if (exclude_primitives) {
    if (!exclude_primitives->empty()) gprtBufferResize(context_, excludePrimitivesBuffer_, exclude_primitives->size(), false);
    gprtBufferMap(excludePrimitivesBuffer_);
    int32_t* gpuExcludedPrimitives = gprtBufferGetHostPointer(excludePrimitivesBuffer_);
    std::copy(exclude_primitives->begin(), exclude_primitives->end(), gpuExcludedPrimitives);
    gprtBufferUnmap(excludePrimitivesBuffer_);

    gprtBufferMap(rayInputBuffer_);
    RayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
    rayInput[0].exclude_primitives = gprtBufferGetDevicePointer(excludePrimitivesBuffer_);
    rayInput[0].exclude_count = exclude_primitives->size();
    gprtBufferUnmap(rayInputBuffer_);
  } 
  else {
    // If no primitives are excluded, set the pointer to null and count to 0
    gprtBufferMap(rayInputBuffer_);
    RayInput* rayInput = gprtBufferGetHostPointer(rayInputBuffer_);
    rayInput[0].exclude_primitives = nullptr;
    rayInput[0].exclude_count = 0;
    gprtBufferUnmap(rayInputBuffer_);
  }


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


void GPRTRayTracer::render_mesh() 
{
    create_world_tlas();

    // Set up ray generation and miss programs
    GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context_, module_, "raygen");
    GPRTMissOf<void> miss = gprtMissCreate<void>(context_, module_, "miss");

    // Place a reference to the TLAS in the ray generation kernel's parameters
    RayGenData* rayGenData = gprtRayGenGetParameters(rayGen);
    rayGenData->world = gprtAccelGetDeviceAddress(world_);

    // Create the framebuffer
    GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context_, fbSize.x * fbSize.y);
    rayGenData->frameBuffer = gprtBufferGetDevicePointer(frameBuffer);

    gprtBuildShaderBindingTable(context_, GPRT_SBT_ALL);

    PushConstants pc;
    do {
        pc.time = float(gprtGetTime(context_));

        // Calls the GPU raygen kernel function
        gprtRayGenLaunch2D(context_, rayGen, fbSize.x, fbSize.y, pc);

        // If a window exists, presents the frame buffer here to that window
        gprtBufferPresent(context_, frameBuffer);
    }
    while (!gprtWindowShouldClose(context_));
}

void GPRTRayTracer::closest(TreeID scene,
                            const Position& origin,
                            double& dist) 
{
  render_mesh();
}

} // namespace xdg
