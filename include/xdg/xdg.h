#ifndef _XDG_INTERFACE_H
#define _XDG_INTERFACE_H

#include <memory>
#include <unordered_map>

#include "xdg/mesh_manager_interface.h"
#include "xdg/ray_tracing_interface.h"


namespace xdg {

struct DeviceRayHitBuffers; // forward declaration
struct dblHit; // forward declaration
class XDG {

public:
  // Constructors
  XDG() = default;

  XDG(std::shared_ptr<MeshManager> mesh_manager, RTLibrary ray_tracing_lib = RTLibrary::EMBREE);

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

//! Returns a vector of segments between the start and end points on the mesh
//! @param start The starting point of the query
//! @param end The ending point of the query
//! @return A vector of pairs containing the element ID and length inside each element
std::vector<std::pair<MeshID, double>>
segments(const Position& start,
         const Position& end) const;

//! Returns a vector of segments between the start and end points on the mesh for a specified volume (subdomain)
//! @param volume The ID of the volume to intersect with
//! @param start The starting point of the query
//! @param end The ending point of the query
//! @return A vector of pairs containing the element ID and length inside each element within the volume
std::vector<std::pair<MeshID, double>>
segments(MeshID volume,
         const Position& start,
         const Position& end) const;

//! Returns the next element along a line
//! @param current_element The current element
//! @param r The starting point of the line
//! @param u The direction of the line
//! @return A pair containing the element ID and length inside the element
std::pair<MeshID, double>
next_element(MeshID current_element,
                  const Position& r,
                  const Direction& u) const;

/**
 * @brief Check whether a point lies in a specified volume
 *
 * This method performs a check to see whether a given point is inside a volume provided.
 * It computes this by firing a ray from the point and checking whether or not the ray is Entering or Exiting
 * the volume boundary. If no direction is provided, a default direction will be used.
 * Note - zero length direction vectors are not explicitly checked for internally and should be avoided to avoid causing undefined behavior.
 * 
 * @param[in] tree The TreeID of the volume we are querying against
 * @param[in] point The point to be queried
 * @param[in] direction (optional) direction to launch a ray in a specified direction - must be non-zero length
 * @param[in] exclude_primitives (optional) vector of surface element MeshIDs to exclude from intersection tests
 * @return Boolean result of point in volume check
 */ 
bool point_in_volume(MeshID volume,
      const Position point,
      const Direction* direction = nullptr,
      const std::vector<MeshID>* exclude_primitives = nullptr) const;

/**
 * @brief Fire a ray against a given volume and return the first hit
 *
 * This method fires a ray from a given origin in a specified direction against the surfaces of a volume.
 * It returns the distance to the closest hit and the MeshID of the surface hit. The user can specify
 * a distance limit and whether Entering/Exiting hits should be rejected.
 * 
 * @param[in] volume The MeshID of the volume we are querying against
 * @param[in] origin Origin of the ray to be fired
 * @param[in] direction (optional) Direction object to launch a ray in a specified direction
 * @param[in] dist_limit (optional) maximum distance to consider for intersections
 * @param[in] orientation (optional) flag to consider whether Entering/Exiting hits should be rejected. Defaults to EXITING
 * @param[in] exclude_primitives (optional) vector of surface element MeshIDs to exclude from intersection tests
 * @return A pair containing the distance to the closest hit and the MeshID of the surface hit
 */ 
std::pair<double, MeshID> ray_fire(MeshID volume,
                                   const Position& origin,
                                   const Direction& direction,
                                   const double dist_limit = INFTY,
                                   HitOrientation orientation = HitOrientation::EXITING,
                                   std::vector<MeshID>* const exclude_primitives = nullptr) const;


/**
 * @brief Call ray fire on pre-populated ray buffers
 *
 * This method performs a set of ray fire queries on a set of rays that have already been populated on device
 * via the external ray population callback method. With GPRT ray tracing this launches the RT pipeline with the number of rays provided.
 *
 * @param[in] num_rays The number of rays to be processed in the batch
 * @param[in] dist_limit (optional) maximum distance to consider for intersections
 * @param[in] orientation (optional) flag to consider whether Entering/Exiting hits should be rejected. Defaults to EXITING
 * @return Void. Outputs stored in dblHit buffer on device.
 */ 
void ray_fire_prepared(const size_t num_rays,
                       const double dist_limit = INFTY,
                       HitOrientation orientation = HitOrientation::EXITING);

/**
 * @brief Call point_in_volume on pre-populated ray buffers
 *
 * This method performs a set of point_in_volume queries on a set of points that have already been populated on device
 * via the external ray population callback method. With GPRT ray tracing this launches the RT pipeline with the number of points provided.
 *
 * @param[in] num_points The number of points to be processed in the batch
 * @return Void. Outputs stored in dblHit buffer on device. 
 */ 
void point_in_volume_prepared(const size_t num_points);

std::pair<double, MeshID> closest(MeshID volume,
                                  const Position& origin) const;

double closest_distance(MeshID volume,
                        const Position& origin) const;

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

  // Resize buffers (if necessary) and return device pointers for ray and hit data
  DeviceRayHitBuffers get_device_rayhit_buffers(const size_t requiredCapacity)
  {
    return ray_tracing_interface()->get_device_rayhit_buffers(requiredCapacity);
  }

  void populate_rays_external(size_t numRays,
                              const RayPopulationCallback& callback)
  {
    return ray_tracing_interface()->populate_rays_external(numRays, callback);
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

  std::unordered_map<MeshID, TreeID> volume_to_surface_tree_map_;  //<! Map from mesh volume to raytracing tree
  std::unordered_map<MeshID, TreeID> surface_to_tree_map_; //<! Map from mesh surface to embree scnee
  std::unordered_map<MeshID, TreeID> volume_to_point_location_tree_map_; //<! Map from mesh volume to embree point location tree
  TreeID global_scene_; // TODO: does this need to be in the RayTacer class or the XDG? class
};

}


#endif
