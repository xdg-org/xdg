#ifndef _XDG_RAY_TRACING_INTERFACE_H
#define _XDG_RAY_TRACING_INTERFACE_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "xdg/constants.h"
#include "xdg/embree_interface.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"

namespace xdg
{

class RayTracer {
public:
  // Constructors/Destructors
  virtual ~RayTracer();

  // Methods
  virtual void init() = 0;

  /**
  * @brief Registers a volume with the ray tracer.
  *
  * This method associates a volume, represented by a MeshID, with the ray
  * tracer using the provided MeshManager. It returns a pair of TreeIDs that can
  * be used for further operations involving the registered volume.
  *
  * @param mesh_manager A shared pointer to the MeshManager responsible for
  * managing the volume's mesh data.
  * @param volume The MeshID representing the volume to be registered.
  * @return A pair of TreeIDs, where the first TreeID corresponds to the surface
  *         ray tracing tree and the second TreeID corresponds to the volume
  *         element point location tree (if applicable).
  */
  virtual std::pair<TreeID, TreeID>
  register_volume(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) = 0;

  /**
   * @brief Creates a surface tree for a given volume.
   *
   * This method creates a ray tracing tree specifically for the surfaces of a given volume.
   * The tree can be used for ray-surface intersection queries.
   *
   * @param mesh_manager A shared pointer to the MeshManager responsible for managing the volume's mesh data.
   * @param volume The MeshID representing the volume whose surfaces will be used to create the tree.
   * @return A TreeID that can be used to reference this surface tree in subsequent ray tracing operations.
   */
  virtual TreeID create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) = 0;

  /**
   * @brief Creates an element tree for a given volume.
   *
   * This method creates a ray tracing tree specifically for the volumetric elements of a given volume.
   * The tree can be used for point-in-element queries.
   *
   * @param mesh_manager A shared pointer to the MeshManager responsible for managing the volume's mesh data.
   * @param volume The MeshID representing the volume whose elements will be used to create the tree.
   * @return A TreeID that can be used to reference this element tree in subsequent point containment operations.
   */
  virtual TreeID create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager, MeshID volume) = 0;

  /**
  * @brief Builds a global tree for all surfaces registered with the ray tracer.
  */
  virtual void create_global_surface_tree() = 0;

  /**
   * @brief Builds a global tree for all elements registered with the ray tracer.
   */
  virtual void create_global_element_tree() = 0;

  // Query Methods
  virtual bool point_in_volume(TreeID tree,
                       const Position& point,
                       const Direction* direction = nullptr,
                       const std::vector<MeshID>* exclude_primitives = nullptr) const = 0;

  virtual std::pair<double, MeshID> ray_fire(TreeID tree,
                                     const Position& origin,
                                     const Direction& direction,
                                     const double dist_limit = INFTY,
                                     HitOrientation orientation = HitOrientation::EXITING,
                                     std::vector<MeshID>* const exclude_primitives = nullptr) = 0;

  // Array based queries
  
  // Array version of point_in_volume
  virtual void batch_point_in_volume(TreeID tree,
                                     const Position* points,
                                     const Direction* directions, // [num_points] array of Direction pointers
                                     const size_t num_points,
                                     uint8_t* results,
                                     const uint8_t* has_dir = nullptr,
                                     std::vector<MeshID>* exclude_primitives = nullptr) = 0;

  // Array version of ray_fire
  virtual void batch_ray_fire(TreeID tree,
                              const Position* origin,
                              const Direction* direction,
                              const size_t num_rays,
                              double* hitDistances,
                              MeshID* surfaceIDs,
                              const double dist_limit = INFTY,
                              HitOrientation orientation = HitOrientation::EXITING,
                              std::vector<MeshID>* const exclude_primitives = nullptr) = 0;

  /**
   * @brief Finds the element containing a given point using the global element tree.
   *
   * This method searches for the element that contains the specified point using
   * the global element tree. It is a convenience wrapper around the tree-specific
   * find_element method.
   *
   * @param point The Position to search for
   * @return The MeshID of the containing element, or ID_NONE if no element contains the point
   */
  virtual MeshID find_element(const Position& point) const = 0;

  /**
   * @brief Finds the element containing a given point using a specific tree.
   *
   * This method searches for the element that contains the specified point using
   * the provided tree. It is a more specific version of the global find_element
   * method.
   */
  virtual MeshID find_element(TreeID tree, const Position& point) const = 0;

  virtual std::pair<double, MeshID> closest(TreeID tree,
                                            const Position& origin) = 0;

  virtual bool occluded(TreeID tree,
                const Position& origin,
                const Direction& direction,
                double& dist) const = 0;

  virtual RTLibrary library() const = 0;


  // Generic Accessors
  int num_registered_trees() const { return surface_trees_.size() + element_trees_.size(); };
  int num_registered_surface_trees() const { return surface_trees_.size(); };
  int num_registered_element_trees() const { return element_trees_.size(); };

protected:
  // Common functions across RayTracers
  const double bounding_box_bump(const std::shared_ptr<MeshManager> mesh_manager, MeshID volume_id); // return a bump value based on the size of a bounding box (minimum 1e-3). Should this be a part of mesh_manager?

  SurfaceTreeID next_surface_tree_id(); // get next surface treeid
  ElementTreeID next_element_tree_id(); // get next element treeid

  // Common member variables across RayTracers

  SurfaceTreeID global_surface_tree_ {TREE_NONE}; //<! TreeID for the global surface tree
  ElementTreeID global_element_tree_ {TREE_NONE}; //<! TreeID for the global element tree

  std::map<MeshID, SurfaceTreeID> surface_to_tree_map_; //<! Map from mesh surface to embree scene
  std::map<MeshID, ElementTreeID> point_location_tree_map_; //<! Map from mesh volume to point location tree

  std::vector<SurfaceTreeID> surface_trees_; //<! All surface trees created by this ray tracer
  std::vector<ElementTreeID> element_trees_; //<! All element trees created by this ray tracer

  // Internal parameters
  SurfaceTreeID next_surface_tree_id_ {0};
  ElementTreeID next_element_tree_id_ {0};
  double numerical_precision_ {1e-3};
};

} // namespace xdg


#endif // include guard