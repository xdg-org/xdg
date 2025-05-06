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
  
      GPRTRayTracer() {
        context_ = gprtContextCreate();
        module_ = gprtModuleCreate(context_, flt_deviceCode);
      };
  
      ~GPRTRayTracer() {
        gprtContextDestroy(context_);
      };
  
      void set_geom_data(const std::shared_ptr<MeshManager> mesh_manager)
      {
        // Create a "triangle" geometry type and set its closest-hit program
        auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context_, GPRT_TRIANGLES);
        gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module_, "TriangleMesh_particle");
  
        std::vector<GPRTBufferOf<float3>> vertex_buffers;
        std::vector<GPRTBufferOf<uint3>> connectivity_buffers;
        std::vector<GPRTGeomOf<TrianglesGeomData>> trianglesGeom;
        std::vector<size_t> vertex_counts;
        std::vector<size_t> index_counts;
  
        for (const auto &surf : mesh_manager->surfaces()) {
          // Get surface mesh vertices and associated connectivities
          auto meshParams = mesh_manager->get_surface_mesh(surf);
          auto vertices = meshParams.first;
          auto indices = meshParams.second;
  
          // Convert vertices to float3 
          std::vector<float3> fl3Vertices;
          fl3Vertices.reserve(vertices.size());    
          for (const auto &vertex : vertices) {
            fl3Vertices.emplace_back(vertex.x, vertex.y, vertex.z);
          }
          vertex_counts.push_back(fl3Vertices.size());
  
          // Convert connectivities/indices to uint3
          std::vector<uint3> ui3Indices;
          ui3Indices.reserve(indices.size() / 3);
          for (size_t i = 0; i < indices.size(); i += 3) {
            ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
          }
          index_counts.push_back(ui3Indices.size());
  
          // Create GPRT buffers and geometry data
          vertex_buffers.push_back(gprtDeviceBufferCreate<float3>(context_, fl3Vertices.size(), fl3Vertices.data()));
          connectivity_buffers.push_back(gprtDeviceBufferCreate<uint3>(context_, ui3Indices.size(), ui3Indices.data()));
          trianglesGeom.push_back(gprtGeomCreate<TrianglesGeomData>(context_, trianglesGeomType));
          TrianglesGeomData* geom_data = gprtGeomGetParameters(trianglesGeom.back());
          geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffers.back());
          geom_data->index = gprtBufferGetDevicePointer(connectivity_buffers.back());
          geom_data->id = surf;
          geom_data->vols = {mesh_manager->get_parent_volumes(surf).first, mesh_manager->get_parent_volumes(surf).second};
        }
      }
  
      void init() override {
        // Initialize GPRT context and modules
      }
  
      TreeID register_volume(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume) override {
        // Register volume with GPRT
        return 0;
      }
  
      bool point_in_volume(TreeID scene,
                          const Position& point,
                          const Direction* direction = nullptr,
                          const std::vector<MeshID>* exclude_primitives = nullptr) const override {
        // Check if point is in volume
        return false;
      }
  
      std::pair<double, MeshID> ray_fire(TreeID scene,
                                        const Position& origin,
                                        const Direction& direction,
                                        const double dist_limit = INFTY,
                                        HitOrientation orientation = HitOrientation::EXITING,
                                        std::vector<MeshID>* const exclude_primitives = nullptr) override {
        // Fire a ray and return the distance to the closest intersection
        return {0.0f, 0};
      }
  
      void closest(TreeID scene,
                  const Position& origin,
                  double& dist,
                  MeshID& triangle) override {
        // Find the closest triangle to the origin
      }
  
      void closest(TreeID scene,
                  const Position& origin,
                  double& dist) override {
        // Find the closest triangle to the origin
      }
  
      bool occluded(TreeID scene,
                    const Position& origin,
                    const Direction& direction,
                    double& dist) const override {
        // Check if the ray is occluded
        return false;
      }
  
      const std::shared_ptr<GeometryUserData>& geometry_data(MeshID surface) const override
      { return user_data_map_.at(surface_to_geometry_map_.at(surface)); };
  
    private:
      GPRTContext context_;
      GPRTProgram deviceCode_; // device code for float precision shaders
      GPRTModule module_; // device code module for single precision shaders
      std::vector<GPRTGeomOf<TrianglesGeomData>> trianglesGeom; // geometry for triangle meshes
      // std::vector<GPRTGeom> geometries_; //<! All geometries created by this ray tracer
    
      int framebufferSize = 0; // Effectively the number of rays to be cast since we do 1D raygen
      
      // Mesh-to-Scene maps 
      std::map<MeshID, GPRTGeom> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

      // Internal GPRT Mappings
      std::unordered_map<GPRTGeom, std::shared_ptr<GeometryUserData>> user_data_map_;

      std::unordered_map<TreeID, GPRTAccel> accel_to_scene_map_; // Map from XDG::TreeID to specific embree scene/tree

      // storage
      std::unordered_map<GPRTAccel, std::vector<PrimitiveRef>> primitive_ref_storage_; // Comes from sharedCode.h?
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