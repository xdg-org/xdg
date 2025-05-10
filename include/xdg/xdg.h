#ifndef _XDG_INTERFACE_H
#define _XDG_INTERFACE_H

#include <memory>
#include <unordered_map>

#include "xdg/mesh_manager_interface.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/embree/ray_tracer.h"

namespace xdg {

class XDG {

public:
  // Constructors
  XDG() = default;

  XDG(std::shared_ptr<MeshManager> mesh_manager, RTLibrary ray_tracing_lib = RTLibrary::EMBREE) : mesh_manager_(mesh_manager)
  {
    // construct internal raytracer for XDG
    switch (ray_tracing_lib)
    {
    case RTLibrary::EMBREE:
      set_ray_tracing_interface(std::make_shared<EmbreeRayTracer>());
      break;
    case RTLibrary::GPRT:
      fatal_error("This backend is not yet implemented");
      break;
    }
  }

  // factory method that allows for specification of a backend mesh library and ray tracer. Default to MOAB + EMBREE
  static std::shared_ptr<XDG> create(MeshLibrary mesh_lib = MeshLibrary::MOAB, RTLibrary ray_tracing_lib = RTLibrary::EMBREE);

  // Methods
  void prepare_raytracer();

  void prepare_volume_for_raytracing(MeshID volume);

// Geometric Queries
MeshID find_volume(const Position& point,
                   const Direction& direction) const;

MeshID find_element(const Position& point) const;

MeshID find_element(MeshID volume,
                    const Position& point) const;

bool point_in_volume(MeshID volume,
                     const Position point,
                     const Direction* direction = nullptr,
                     const std::vector<MeshID>* exclude_primitives = nullptr) const;

std::pair<double, MeshID> ray_fire(MeshID volume,
                                   const Position& origin,
                                   const Direction& direction,
                                   const double dist_limit = INFTY,
                                   HitOrientation orientation = HitOrientation::EXITING,
                                   std::vector<MeshID>* const exclude_primitives = nullptr) const;

std::vector<std::pair<MeshID, double>>
segments(MeshID volume,
        const Position& start,
        const Position& end) const;

void closest(MeshID volume,
              const Position& origin,
              double& dist,
              MeshID& triangle) const;

void closest(MeshID volume,
              const Position& origin,
              double& dist) const;

bool occluded(MeshID volume,
              const Position& origin,
              const Direction& direction,
              double& dist) const;

Direction surface_normal(MeshID surface,
                         Position point,
                         const std::vector<MeshID>* exclude_primitives = nullptr) const;


  // Geometric Measurements
  double measure_volume(MeshID volume) const;
  double measure_surface_area(MeshID surface) const;
  double measure_volume_area(MeshID surface) const;

// Mutators
  void set_mesh_manager_interface(std::shared_ptr<MeshManager> mesh_manager) {
    mesh_manager_ = mesh_manager;
  }

  void set_ray_tracing_interface(std::shared_ptr<RayTracer> ray_tracing_interface) {
    ray_tracing_interface_ = ray_tracing_interface;
  }

// Accessors
  const std::shared_ptr<RayTracer>& ray_tracing_interface() const {
    return ray_tracing_interface_;
  }

  const std::shared_ptr<MeshManager>& mesh_manager() const {
    return mesh_manager_;
  }
// Private methods
private:
  double _triangle_volume_contribution(const PrimitiveRef& triangle) const;
  double _triangle_area_contribution(const PrimitiveRef& triangle) const;

// Data members
  std::shared_ptr<RayTracer> ray_tracing_interface_ {nullptr};
  std::shared_ptr<MeshManager> mesh_manager_ {nullptr};

  std::unordered_map<MeshID, TreeID> volume_to_surface_tree_map_;  //<! Map from mesh volume to embree scene
  std::unordered_map<MeshID, TreeID> surface_to_tree_map_; //<! Map from mesh surface to embree scnee
  std::unordered_map<MeshID, TreeID> volume_to_point_location_tree_map_; //<! Map from mesh volume to embree point location tree
  std::unordered_map<MeshID, RTCGeometry> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry
  TreeID global_scene_; // TODO: does this need to be in the RayTacer class or the XDG? class
};

}


#endif
