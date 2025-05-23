#ifndef _XDG_GPRT_BASE_RAY_TRACING_INTERFACE_H
#define _XDG_GPRT_BASE_RAY_TRACING_INTERFACE_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/ray.h"
#include "xdg/error.h"
#include "gprt/gprt.h"
#include "sharedCode.h"

extern GPRTProgram flt_deviceCode;

namespace xdg {

  class GPRTRayTracer : public RayTracer {
    public:
      GPRTRayTracer();
      ~GPRTRayTracer();
  
      void set_geom_data(const std::shared_ptr<MeshManager> mesh_manager);
      void create_world_tlas();
  
      void init() override;

      // Setup the different shader programs for use with this ray tracer
      void setup_shaders();

  
      TreeID register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume) override;

      bool point_in_volume(TreeID scene,
                          const Position& point,
                          const Direction* direction = nullptr,
                          const std::vector<MeshID>* exclude_primitives = nullptr) const override;
  
      std::pair<double, MeshID> ray_fire(TreeID scene,
                                        const Position& origin,
                                        const Direction& direction,
                                        const double dist_limit = INFTY,
                                        HitOrientation orientation = HitOrientation::EXITING,
                                        std::vector<MeshID>* const exclude_primitives = nullptr) override;
        // Fire a ray and return the distance to the closest intersection




      void closest(TreeID scene,
                  const Position& origin,
                  double& dist,
                  MeshID& triangle) override {
        // Find the closest triangle to the origin
      }
  
      void closest(TreeID scene,
                  const Position& origin,
                  double& dist) override;
  
      bool occluded(TreeID scene,
                    const Position& origin,
                    const Direction& direction,
                    double& dist) const override {
        // Check if the ray is occluded
        return false;
      }
  
      const std::shared_ptr<GeometryUserData>& geometry_data(MeshID surface) const override
      { return user_data_map_.at(surface_to_geometry_map_.at(surface)); };
  
      void render_mesh();

    private:
      GPRTContext context_;
      GPRTProgram deviceCode_; // device code for float precision shaders
      GPRTModule module_; // device code module for single precision shaders
      // std::vector<GPRTGeom> geometries_; //<! All geometries created by this ray tracer
      GPRTAccel world_; 
      GPRTRayGenOf<RayGenData> rayGenProgram_; //<! Ray generation program
      GPRTRayGenOf<RayGenData> rayGenPointInVolProgram_;
      GPRTMissOf<void> missProgram_; //<! Miss program
      GPRTBufferOf<uint32_t> frameBuffer_; //<! Framebuffer
      int2 fbSize; //<! Size of the framebuffer 
      GPRTBufferOf<RayInput> rayInputBuffer_; //<! Ray buffer for ray generation
      GPRTBufferOf<RayOutput> rayOutputBuffer_; //<! Ray output buffer for ray generation
      GPRTBufferOf<int32_t> excludePrimitivesBuffer_; //<! Buffer for excluded primitives
      size_t numRays = 1; //<! Number of rays to be cast
      uint32_t numRayTypes_ = 2; // <! Number of ray types. Allows multiple shaders to be set to the same geometery
      std::vector<gprt::Instance> globalBlasInstances_; //<! List of every BLAS instance stored in this ray tracer

      // Mesh-to-Scene maps 
      std::map<MeshID, GPRTGeom> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

      // Internal GPRT Mappings
      std::unordered_map<GPRTGeom, std::shared_ptr<GeometryUserData>> user_data_map_;

      std::unordered_map<TreeID, GPRTAccel> tree_to_vol_accel_map; // Map from XDG::TreeID to GPRTAccel for volume TLAS

      // storage
      std::unordered_map<GPRTAccel, std::vector<PrimitiveRef>> primitive_ref_storage_; // Comes from sharedCode.h?
      std::vector<GPRTBufferOf<float3>> vertex_buffers; // <! vertex buffers for each geometry
      std::vector<GPRTBufferOf<uint3>> connectivity_buffers; // <! connectivity buffers for each geometry

    };

} // namespace xdg


// template<typename FloatPrecision>
// class GPRTRayTracer : public RayTracer {
//   // constructors
// public:
//   // GPRTRayTracer();
//   // ~GPRTRayTracer();
  
//   void init() override;

// //   void add_module(GPRTProgam device_code, std::string name);
//   TreeID register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume) override;

//   // Query Methods
//   bool point_in_volume(TreeID scene,
//                       const Position& point,
//                       const Direction* direction = nullptr,
//                       const std::vector<MeshID>* exclude_primitives = nullptr) const override;


//   std::pair<FloatPrecision, MeshID> ray_fire(TreeID scene,
//                                      const Position& origin,
//                                      const Direction& direction,
//                                      const FloatPrecision dist_limit = INFTY,
//                                      HitOrientation orientation = HitOrientation::EXITING,
//                                      std::vector<MeshID>* const exclude_primitives = nullptr) override;

//   void closest(TreeID scene,
//                const Position& origin,
//                FloatPrecision& dist,
//                MeshID& triangle) override;

//   void closest(TreeID scene,
//                const Position& origin,
//                FloatPrecision& dist) override;

//   bool occluded(TreeID scene,
//                 const Position& origin,
//                 const Direction& direction,
//                 FloatPrecision& dist) const override;

//   // Accessors
//   const std::shared_ptr<GeometryUserData>& geometry_data(MeshID surface) const override
//   { return user_data_map_.at(surface_to_geometry_map_.at(surface)); };

//   // GPRT members
// protected:
//   GPRTContext context_;
//   std::vector<GPRTGeom> geometries_; //<! All geometries created by this ray tracer
  
//   int framebufferSize = 0; // Effectively the number of rays to be cast since we do 1D raygen
  
//   // Mesh-to-Scene maps 
//   std::map<MeshID, GPRTGeom> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

//   // Internal GPRT Mappings
//   std::unordered_map<GPRTGeom, std::shared_ptr<GeometryUserData>> user_data_map_;

//   std::unordered_map<TreeID, GPRTAccel> accel_to_scene_map_; // Map from XDG::TreeID to specific embree scene/tree

//   // storage
//   std::unordered_map<GPRTAccel, std::vector<PrimitiveRef>> primitive_ref_storage_; // Comes from sharedCode.h?
// };

// } // namespace xdg

// // Include template specilizations after the class definition
// #include "flt_ray_tracer.h"
// #include "dbl_ray_tracer.h"


#endif // include guard