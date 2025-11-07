#ifndef _XDG_EMBREE_RAY_TRACING_INTERFACE_H
#define _XDG_EMBREE_RAY_TRACING_INTERFACE_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "xdg/constants.h"
#include "xdg/embree_interface.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/ray.h"
#include "xdg/error.h"

namespace xdg {

class EmbreeRayTracer : public RayTracer {
  // constructors
public:
  EmbreeRayTracer();
  ~EmbreeRayTracer();
  RTLibrary library() const override { return RTLibrary::EMBREE; }

  void init() override;
  RTCScene create_embree_scene();

  std::pair<TreeID, TreeID> register_volume(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) override;

  TreeID create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) override;

  TreeID create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) override;

  void create_global_surface_tree() override;

  void create_global_element_tree() override;

  MeshID find_element(const Position& point) const override;

  MeshID find_element(TreeID tree, const Position& point) const override;


  // Query Methods
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
                                     std::vector<MeshID>* exclude_primitives = nullptr) override
  {
    fatal_error("Batch point_in_volume not yet implemented for EmbreeRayTracer");
  };

  // Array version of ray_fire
  void batch_ray_fire(TreeID tree,
                              const Position* origins,
                              const Direction* directions,
                              const size_t num_rays,
                              double* hitDistances,
                              MeshID* surfaceIDs,
                              const double dist_limit = INFTY,
                              HitOrientation orientation = HitOrientation::EXITING,
                              std::vector<MeshID>* const exclude_primitives = nullptr) override
  {
    fatal_error("Batch ray_fire not yet implemented for EmbreeRayTracer");
  };

  std::pair<double, MeshID> closest(TreeID scene,
                                    const Position& origin) override;

  bool occluded(TreeID scene,
                const Position& origin,
                const Direction& direction,
                double& dist) const override;

  // Embree members
  RTCDevice device_;
  std::vector<RTCGeometry> geometries_; //<! All geometries created by this ray tracer

  // Mesh-to-Scene maps
  std::map<MeshID, RTCGeometry> surface_to_geometry_map_; //<! Map from mesh surface to embree geometry

  // Internal Embree Mappings
  std::unordered_map<RTCGeometry, std::shared_ptr<SurfaceUserData>> surface_user_data_map_;
  std::unordered_map<RTCGeometry, std::shared_ptr<VolumeElementsUserData>> volume_user_data_map_;

  std::unordered_map<SurfaceTreeID, RTCScene> surface_volume_tree_to_scene_map_; // Map from SurfaceVolumeTreeID to specific embree scene/tree
  std::unordered_map<ElementTreeID, RTCScene> element_volume_tree_to_scene_map_; // Map from ElementVolumeTreeID to specific embree scene/tree

  // storage
  std::unordered_map<RTCScene, std::vector<PrimitiveRef>> primitive_ref_storage_;

private:
  std::pair<RTCGeometry, std::shared_ptr<SurfaceUserData>> register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                                                                             MeshID surface,
                                                                             RTCScene& volume_scene,
                                                                             int& storage_offset);
  // Global Tree IDs
  RTCScene global_surface_scene_ {nullptr};
  RTCScene global_element_scene_ {nullptr};

};

} // namespace xdg


#endif // include guard