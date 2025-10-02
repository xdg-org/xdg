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
#include "shared_structs.h"

extern GPRTProgram dbl_deviceCode;
namespace xdg {

class GPRTRayTracer : public RayTracer {
  public:
    GPRTRayTracer();
    ~GPRTRayTracer();
    RTLibrary library() const override { return RTLibrary::GPRT; }

    void set_geom_data(const std::shared_ptr<MeshManager> mesh_manager);
    void init() override;

    // Setup the different shader programs for use with this ray tracer
    void setup_shaders();

    MeshID find_element(const Position& point) const override
    {
      fatal_error("Element trees not currently supported with GPRT ray tracer");
      return ID_NONE;
    };

    MeshID find_element(TreeID tree, const Position& point) const override {
      fatal_error("Element trees not currently supported with GPRT ray tracer");
      return ID_NONE;
    };

    std::pair<TreeID, TreeID>
    register_volume(const std::shared_ptr<MeshManager>& mesh_manager, 
                    MeshID volume) override;

    TreeID create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager, 
                               MeshID volume) override;

    TreeID create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager, 
                               MeshID volume) override;

    void create_global_surface_tree() override;

    void create_global_element_tree() override
    {
      warning("Global element trees not currently supported with GPRT ray tracer");
      return;
    };

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
      fatal_error("Occlusion queries are not currently supported with GPRT ray tracer");
      return false;
    }
    
  private:
    // GPRT objects 
    GPRTContext context_;
    GPRTProgram deviceCode_; // device code for float precision shaders
    GPRTModule module_; // device code module for single precision shaders
    GPRTAccel world_; 
    GPRTBuildParams buildParams_; //<! Build parameters for acceleration structures

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
    uint32_t numRayTypes_ = 1; // <! Number of ray types. Allows multiple shaders to be set to the same geometery
    
    // Mesh-to-Scene maps 
    std::map<MeshID, GPRTGeomOf<DPTriangleGeomData>> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

    // Internal GPRT Mappings
    std::unordered_map<SurfaceTreeID, GPRTAccel> surface_volume_tree_to_accel_map; // Map from XDG::TreeID to GPRTAccel for volume TLAS
    std::vector<GPRTAccel> blas_handles_; // Store BLAS handles so that they can be explicitly referenced in destructor

    // Global Tree IDs
    GPRTAccel global_surface_accel_ {nullptr};
    GPRTAccel global_element_accel_ {nullptr}; 
  
  };

} // namespace xdg

#endif // include guard