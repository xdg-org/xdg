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

struct EmbreeSurfaceCache {
  RTCScene scene {nullptr};
  std::shared_ptr<SurfaceUserData> user_data {nullptr};
  std::vector<PrimitiveRef> prim_refs;
};

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

  std::pair<double, MeshID> closest(TreeID scene,
                                    const Position& origin) override;

  bool occluded(TreeID scene,
                const Position& origin,
                const Direction& direction,
                double& dist) const override;

  // Embree members
  RTCDevice device_;

  // Storage for surface caches (RTCScenes, User Data, PrimitiveRefs)
  std::unordered_map<MeshID, EmbreeSurfaceCache> surface_cache_map_; //<! Cache of surfaces already registered with embree
  
  // Internal Embree Mappings
  std::unordered_map<RTCGeometry, std::shared_ptr<VolumeElementsUserData>> volume_user_data_map_;

  std::unordered_map<SurfaceTreeID, RTCScene> surface_volume_tree_to_scene_map_; // Map from SurfaceVolumeTreeID to specific embree scene/tree
  std::unordered_map<ElementTreeID, RTCScene> element_volume_tree_to_scene_map_; // Map from ElementVolumeTreeID to specific embree scene/tree

  // storage (TODO - No longer required for surface elements but still needed for volumetric element primitive storage)
  std::unordered_map<RTCScene, std::vector<PrimitiveRef>> primitive_ref_storage_;

private:
  EmbreeSurfaceCache register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                                      MeshID surface);
  // Global Tree IDs
  RTCScene global_surface_scene_ {nullptr};
  RTCScene global_element_scene_ {nullptr};

};



} // namespace xdg


#endif // include guard