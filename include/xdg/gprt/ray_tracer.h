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

enum class RayGenType {
  RAY_FIRE,
  POINT_IN_VOLUME,
  OCCLUDED,
  CLOSEST
};

struct gprtRayHit {
  size_t capacity = 1; // Max number of rays allocated 
  size_t size = 0;     // Current number of active rays 

  GPRTBufferOf<dblRay> ray = nullptr;
  GPRTBufferOf<dblHit> hit = nullptr;
  dblRay* devRayAddr = nullptr;
  dblHit* devHitAddr = nullptr;

  bool is_valid() const { return capacity > 0 && ray && hit && devRayAddr && devHitAddr; }
};

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

      // Array version of point_in_volume
      void batch_point_in_volume(TreeID tree,
                                        const Position* points,
                                        const Direction* directions, // [num_points] array of Direction pointers
                                        const size_t num_points,
                                        uint8_t* results,
                                        const uint8_t* has_dir = nullptr,
                                        std::vector<MeshID>* exclude_primitives = nullptr) const override;

      // Array version of ray_fire
      void batch_ray_fire(TreeID tree,
                                  const Position* origins,
                                  const Direction* directions,
                                  const size_t num_rays,
                                  double* hitDistances,
                                  MeshID* surfaceIDs,
                                  const double dist_limit = INFTY,
                                  HitOrientation orientation = HitOrientation::EXITING,
                                  std::vector<MeshID>* const exclude_primitives = nullptr) override;

    std::pair<double, MeshID> closest(TreeID scene,
                                      const Position& origin) override {};

    bool occluded(TreeID scene,
                  const Position& origin,
                  const Direction& direction,
                  double& dist) const override {
      fatal_error("Occlusion queries are not currently supported with GPRT ray tracer");
      return false;
    }
    
  private:
    void check_ray_buffer_capacity(size_t N);

    // GPRT objects 
    GPRTContext context_;
    GPRTProgram deviceCode_; // device code for float precision shaders
    GPRTModule module_; // device code module for single precision shaders
    GPRTAccel world_; 
    GPRTBuildParams buildParams_; //<! Build parameters for acceleration structures

    // Shader programs
    std::map<RayGenType, GPRTRayGenOf<dblRayGenData>> rayGenPrograms_;

    GPRTMissOf<void> missProgram_; 
    GPRTComputeOf<DPTriangleGeomData> aabbPopulationProgram_; //<! AABB population program for double precision rays
    
    // Buffers 
    gprtRayHit rayHitBuffers_;
    GPRTBufferOf<int32_t> excludePrimitivesBuffer_; //<! Buffer for excluded primitives
    
    // Geometry Type and Instances
    std::vector<gprt::Instance> globalBlasInstances_; //<! List of every BLAS instance stored in this ray tracer
    GPRTGeomTypeOf<DPTriangleGeomData> trianglesGeomType_; //<! Geometry type for triangles

    // Ray Generation parameters
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