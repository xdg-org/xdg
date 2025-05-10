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
  void init() override;
  RTCScene create_embree_scene();

  std::pair<TreeID, TreeID>
  register_volume(const std::shared_ptr<MeshManager> mesh_manager,
                  MeshID volume) override;

  void create_global_surface_tree() override;
  void create_global_element_tree() override;

  TreeID create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                             MeshID volume);

  TreeID create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                             MeshID volume);

  // Query Methods
  bool point_in_volume(TreeID scene,
                      const Position& point,
                      const Direction* direction = nullptr,
                      const std::vector<MeshID>* exclude_primitives = nullptr) const override;

  MeshID find_element(const Position& point) const override;

  MeshID find_element(TreeID scene,
                      const Position& point) const;

  std::pair<double, MeshID> ray_fire(TreeID scene,
                                     const Position& origin,
                                     const Direction& direction,
                                     const double dist_limit = INFTY,
                                     HitOrientation orientation = HitOrientation::EXITING,
                                     std::vector<MeshID>* const exclude_primitives = nullptr) override;

  void closest(TreeID scene,
               const Position& origin,
               double& dist,
               MeshID& triangle) override;

  void closest(TreeID scene,
               const Position& origin,
               double& dist) override;

  bool occluded(TreeID scene,
                const Position& origin,
                const Direction& direction,
                double& dist) const override;

  // Embree members
  RTCDevice device_;
  std::vector<RTCGeometry> geometries_; //<! All geometries created by this ray tracer

  // Internal Embree Mappings
  std::unordered_map<RTCGeometry, std::shared_ptr<SurfaceUserData>> surface_user_data_map_;
  std::unordered_map<RTCGeometry, std::shared_ptr<VolumeElementsUserData>> volume_user_data_map_;

  std::unordered_map<TreeID, RTCScene> tree_to_scene_map_; // Map from XDG::TreeID to specific embree scene/tree

  RTCScene global_surface_scene_ {nullptr}; //<! Global surface tree for point location
  RTCScene global_element_scene_ {nullptr}; //<! Global element tree for point location

  // storage
  std::unordered_map<RTCScene, std::vector<PrimitiveRef>> primitive_ref_storage_;
};

} // namespace xdg


#endif // include guard