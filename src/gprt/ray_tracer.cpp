// #include "xdg/gprt/ray_tracer.h"


// namespace xdg {

// GPRTRayTracer::GPRTRayTracer()
// {
//   context_ = gprtContextCreate();
// }

// GPRTRayTracer::~GPRTRayTracer()
// {
//   gprtContextDestroy(context_);
// }

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

// std::pair<double, MeshID> GPRTRayTracer::ray_fire(TreeID scene,
//                                                   const Position& origin,
//                                                   const Direction& direction,
//                                                   const double dist_limit,
//                                                   HitOrientation orientation,
//                                                   std::vector<MeshID>* const exclude_primitives) {
//   // TODO: Ray cast logic
//   /*
//   PushConstants pc;
//   pc.origin = origin;
//   pc.direction = direction;
//   pc.dist_limit = dist_limit;
//   pc.orientation = orientation;
  
//   // Need a way of passing the number of particles to this function so that I can generate that many rays
//   nRays = <passed_parameter>;
//   gprtRayGenLaunch1D(context_, rayGen, fbSize.x, nRays, pc);
  

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

// } // namespace xdg
