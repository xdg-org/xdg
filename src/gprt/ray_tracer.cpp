#include "xdg/gprt/ray_tracer.h"


namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
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
  rayGenProgram_ = gprtRayGenCreate<RayGenData>(context_, module_, "raygen");
  missProgram_ = gprtMissCreate<void>(context_, module_, "miss");

  // New: Here, we place a reference to our TLAS in the ray generation
  // kernel's parameters, so that we can access that tree when
  // we go to trace our rays.
  RayGenData *rayGenData = gprtRayGenGetParameters(rayGenProgram_);
  rayGenData->world = gprtAccelGetDeviceAddress(world_);

  rayGenData->frameBuffer = gprtBufferGetDevicePointer(frameBuffer_);
}

TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);


  // Create a "triangle" geometry type and set its closest-hit program
  auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context_, GPRT_TRIANGLES);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module_, "TriangleMesh_particle");

  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);
  std::vector<gprt::Instance> blasInstances; // BLAS for each (surface) geometry

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
    geom_data->vols = {mesh_manager->get_parent_volumes(surf).first, mesh_manager->get_parent_volumes(surf).second};
    
    // Set vertices and indices for the triangle geometry
    gprtTrianglesSetVertices(triangleGeom, vertex_buffer, fl3Vertices.size());
    gprtTrianglesSetIndices(triangleGeom, connectivity_buffer, ui3Indices.size());

    // Create a BLAS for the triangle geometry
    GPRTAccel blas = gprtTriangleAccelCreate(context_, triangleGeom, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
    gprtAccelBuild(context_, blas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
    
    // Store the BLAS for concatenation into the TLAS
    blasInstances.push_back(gprtAccelGetInstance(blas));
    }

    // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances in this volume
    auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, blasInstances.size(), blasInstances.data());
    GPRTAccel volume_tlas = gprtInstanceAccelCreate(context_, blasInstances.size(), instanceBuffer);
    gprtAccelBuild(context_, volume_tlas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
    tree_to_vol_accel_map[tree] = volume_tlas;
    return tree;
  }

void GPRTRayTracer::create_world_tlas()
{
  // Create buffer of volume instances
  std::vector<gprt::Instance> volInstances;
  for (const auto& [tree,volAccel] : tree_to_vol_accel_map) {
    volInstances.push_back(gprtAccelGetInstance(volAccel));
  }

  // Create a TLAS (Top-Level Acceleration Structure) for all the volumes
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context_, volInstances.size(), volInstances.data());
  world_ = gprtInstanceAccelCreate(context_, volInstances.size(), instanceBuffer);
  gprtAccelBuild(context_, world_, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
}

// // Ray tracer interface stub methods to be implemented
// void GPRTRayTracer::init() {
//   // TODO: Init GPRT context and modules
// }

// TreeID GPRTRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume) {
//   // TODO: Register GPRT geometry
//   return {}; // placeholder
// }

// bool GPRTRayTracer::point_in_volume(TreeID scene,
//                                      const Position& point,
//                                      const Direction* direction,
//                                      const std::vector<MeshID>* exclude_primitives) const {
//   // TODO: Point containment logic
//   return false;
// }
  

//   void GPRTRayTracer::ray_fire(TreeID scene,
//                              const std::vector<Position>& origins,
//                              const std::vector<Direction>& directions,
//                              double dist_limit,
//                              HitOrientation orientation,
//                              const std::vector<MeshID>* exclude_primitives) {
//     // Create buffer to store ray data
//     VkBuffer rayBuffer;
//     VkDeviceMemory rayBufferMemory;
//     // Allocate and fill rayBuffer with origins and directions...
//     std::vector<GPRTBufferOf<float3>> particleOriginBuffer;
//     std::vector<GPRTBufferOf<float3>> particleDirectionBuffer;
//     for (size_t i = 0; i < origins.size(); ++i) {
//         particleOriginBuffer.push_back(gprtDeviceBufferCreate<float3>(context_, origins[i]));
//         particleDirectionBuffer.push_back(gprtDeviceBufferCreate<float3>(context_, directions[i]));
//     }
//     // Need to think about handling single vs double precision floating points

//     RayFirePushConstants pc;
//     pc.dist_limit = dist_limit;
//     pc.orientation = orientation;
//     pc.num_rays = origins.size();

//     // Launch the ray generation shader with push constants and buffer bindings
//     gprtRayGenLaunch1D(context_, rayGen, origins.size(), rayBuffer, pc);

//     // This will launch the rays and run our shaders in the ray tracing pipeline
//     // miss shader returns dist = 0.0 and elementID = -1
//     // closest hit shader returns dist = distance to hit and elementID = triangle ID
// }
//   */
                                                  

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

} // namespace xdg
