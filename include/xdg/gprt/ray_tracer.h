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

// extern GPRTProgram flt_deviceCode;
extern GPRTProgram dbl_deviceCode;

namespace xdg {

  class GPRTRayTracer : public RayTracer {
    public:
      GPRTRayTracer();
      ~GPRTRayTracer();
  
      void set_geom_data(const std::shared_ptr<MeshManager> mesh_manager);
      void create_world_tlas();
  
      TreeID register_triangle_geometry(const std::shared_ptr<MeshManager> mesh_manager, MeshID surface);
      TreeID register_AABB_geometry(const std::shared_ptr<MeshManager> mesh_manager, MeshID surface);

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
                  MeshID& triangle) override {};
      void closest(TreeID scene,
                  const Position& origin,
                  double& dist) override {};
  
      bool occluded(TreeID scene,
                    const Position& origin,
                    const Direction& direction,
                    double& dist) const override {
        // Check if the ray is occluded
        return false;
      }
  
      // const std::shared_ptr<GeometryUserData>& geometry_data(MeshID surface) const override
      // { return user_data_map_.at(surface_to_geometry_map_.at(surface)); };

      const std::shared_ptr<GeometryUserData>& geometry_data(MeshID surface) const override
      { };
  

    private:
      // GPRT objects 
      GPRTContext context_;
      GPRTProgram deviceCode_; // device code for float precision shaders
      GPRTModule module_; // device code module for single precision shaders
      GPRTAccel world_; 

      // Shader programs
      GPRTRayGenOf<dblRayGenData> rayGenProgram_; 
      GPRTRayGenOf<dblRayGenData> rayGenPointInVolProgram_;
      GPRTMissOf<void> missProgram_; 
      GPRTComputeOf<DPTriangleGeomData> aabbPopulationProgram_; //<! AABB population program for double precision rays
      
      // Buffers 
      GPRTBufferOf<dblRayInput> rayInputBuffer_; //<! Ray buffer for ray generation
      GPRTBufferOf<dblRayOutput> rayOutputBuffer_; //<! Ray output buffer for ray generation
      GPRTBufferOf<int32_t> excludePrimitivesBuffer_; //<! Buffer for excluded primitives
      
      // Geometry Type and Instances
      std::vector<gprt::Instance> globalBlasInstances_; //<! List of every BLAS instance stored in this ray tracer
      GPRTGeomTypeOf<DPTriangleGeomData> trianglesGeomType_; //<! Geometry type for triangles

      // Ray Generation parameters
      size_t numRays = 1; //<! Number of rays to be cast
      uint32_t numRayTypes_ = 2; // <! Number of ray types. Allows multiple shaders to be set to the same geometery
      
      // Mesh-to-Scene maps 
      std::map<MeshID, GPRTGeomOf<DPTriangleGeomData>> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

      // Internal GPRT Mappings
      std::unordered_map<GPRTGeom, std::shared_ptr<GeometryUserData>> user_data_map_;
      std::unordered_map<TreeID, GPRTAccel> tree_to_vol_accel_map; // Map from XDG::TreeID to GPRTAccel for volume TLAS
    };

} // namespace xdg

#endif // include guard