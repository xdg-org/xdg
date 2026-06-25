#ifndef _XDG_CUBQL_RAY_TRACING_INTERFACE_H
#define _XDG_CUBQL_RAY_TRACING_INTERFACE_H

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xdg/constants.h"
#include "xdg/geometry_data.h"
#include "xdg/cuBQL/triangles.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/ray.h"
#include "xdg/ray_tracing_interface.h"

namespace xdg {

struct CuBQLRay;
struct CuBQLSurfaceHit;

class CuBQLRayTracer : public RayTracer {
public:
  CuBQLRayTracer();
  ~CuBQLRayTracer() override;

  RTLibrary library() const override { return RTLibrary::CUBQL; }

  void init() override;

  std::pair<TreeID, TreeID>
  register_volume(const std::shared_ptr<MeshManager>& mesh_manager,
                  MeshID volume) override;

  TreeID create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                             MeshID volume) override;

  TreeID create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                             MeshID volume) override;

  void create_global_surface_tree() override;

  void create_global_element_tree() override;

  MeshID find_element(const Position& point) const override;

  MeshID find_element(TreeID tree, const Position& point) const override;

  bool point_in_volume(TreeID tree,
                       const Position& point,
                       const Direction* direction = nullptr,
                       const std::vector<MeshID>* exclude_primitives = nullptr) const override;

  std::pair<double, MeshID> ray_fire(TreeID tree,
                                     const Position& origin,
                                     const Direction& direction,
                                     const double dist_limit = INFTY,
                                     HitOrientation orientation = HitOrientation::EXITING,
                                     std::vector<MeshID>* const exclude_primitives = nullptr) override;

  void ray_fire_batch(const XDGRayHitBuffer& ray_hits,
                      HitOrientation hit_orientation = HitOrientation::EXITING) const override;

  XDGRayHitBuffer allocate_ray_hits(std::size_t count) const override;

  void free_ray_hits(XDGRayHitBuffer& ray_hits) const override;

  std::pair<double, MeshID> closest(TreeID tree,
                                    const Position& origin) override;

  bool occluded(TreeID tree,
                const Position& origin,
                const Direction& direction,
                double& dist) const override;

private:
  CuBQLSurfaceBLAS
  register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                   MeshID surface_id,
                   double bounding_box_bump);

  void upload_volume_to_tlas_table_();

  cubql::Context context_;

  std::unordered_map<TreeID, CuBQLVolumeTLAS> tree_to_volume_tlas_;
  std::unordered_map<MeshID, CuBQLSurfaceBLAS> surface_to_blas_map_;

  std::vector<CuBQLVolumeTLAS::DD> volume_to_tlas_;
  CuBQLVolumeTLAS::DD* d_volume_to_tlas_ {nullptr};
  bool initialized_ {false};
};

} // namespace xdg

#endif // include guard
