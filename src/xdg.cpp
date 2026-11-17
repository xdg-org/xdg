#include <vector>
#include <numeric>

#include "xdg/xdg.h"
#include "xdg/error.h"
#include "xdg/constants.h"
#include "xdg/geometry/measure.h"

#include "xdg/mesh_managers.h"

#include "xdg/ray_tracers.h"

namespace xdg {

XDG::XDG(std::shared_ptr<MeshManager> mesh_manager, RTLibrary ray_tracing_lib)
        : mesh_manager_(mesh_manager)
{
  switch (ray_tracing_lib) {
    case RTLibrary::EMBREE:
    #ifdef XDG_ENABLE_EMBREE
      set_ray_tracing_interface(std::make_shared<EmbreeRayTracer>());
      break;
    #else
      fatal_error("This build was not compiled with Embree support (XDG_ENABLE_EMBREE=OFF).");
    #endif

    case RTLibrary::GPRT:
    #ifdef XDG_ENABLE_GPRT
      set_ray_tracing_interface(std::make_shared<GPRTRayTracer>());
      break;
    #else
      fatal_error("This build was not compiled with GPRT support (XDG_ENABLE_GPRT=OFF).");
    #endif
  }
}

void XDG::prepare_raytracer()
{
  for (auto volume : mesh_manager()->volumes()) {
    this->prepare_volume_for_raytracing(volume);
  }

  ray_tracing_interface()->create_global_element_tree();
  ray_tracing_interface()->create_global_surface_tree();

  ray_tracing_interface()->init(); // Initialize the ray tracer (e.g. build SBT for GPRT)
}

void XDG::prepare_volume_for_raytracing(MeshID volume) {
    auto [surface_tree, volume_tree] = ray_tracing_interface_->register_volume(mesh_manager_, volume);
    volume_to_surface_tree_map_[volume] = surface_tree;
    volume_to_point_location_tree_map_[volume] = volume_tree;
}

std::shared_ptr<XDG> XDG::create(MeshLibrary mesh_lib, RTLibrary ray_tracing_lib)
{
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>();

  // Mesh factory dispatch
  auto mesh_factory = [&]() -> std::shared_ptr<MeshManager> {
    #ifdef XDG_ENABLE_MOAB
    if (mesh_lib == MeshLibrary::MOAB) return std::make_shared<MOABMeshManager>();
    #endif
    #ifdef XDG_ENABLE_LIBMESH
    if (mesh_lib == MeshLibrary::LIBMESH) return std::make_shared<LibMeshManager>();
    #endif

    // If no supported mesh library throw an error
    std::string msg = fmt::format("Invalid mesh library '{}'. Supported:", MESH_LIB_TO_STR.at(mesh_lib));
    #ifdef XDG_ENABLE_MOAB
    msg += " MOAB";
    #endif
    #ifdef XDG_ENABLE_LIBMESH
    msg += " LIBMESH";
    #endif
    fatal_error(msg);
    return nullptr;
  };

  // Ray tracer factory dispatch
  auto rt_factory = [&]() -> std::shared_ptr<RayTracer> {
    #ifdef XDG_ENABLE_EMBREE
    if (ray_tracing_lib == RTLibrary::EMBREE) return std::make_shared<EmbreeRayTracer>();
    #endif
    #ifdef XDG_ENABLE_GPRT
    if (ray_tracing_lib == RTLibrary::GPRT) return std::make_shared<GPRTRayTracer>();
    #endif

    // If no supported ray tracing library throw an error
    std::string msg = fmt::format("Invalid ray tracing library '{}'. Supported:", RT_LIB_TO_STR.at(ray_tracing_lib));
    #ifdef XDG_ENABLE_EMBREE
    msg += " EMBREE";
    #endif
    #ifdef XDG_ENABLE_GPRT
    msg += " GPRT";
    #endif
    fatal_error(msg);
    return nullptr;
  };

  xdg->set_mesh_manager_interface(mesh_factory());
  xdg->set_ray_tracing_interface(rt_factory());
  return xdg;
}

bool XDG::point_in_volume(MeshID volume,
                          const Position point,
                          const Direction* direction,
                          const std::vector<MeshID>* exclude_primitives) const
{
  TreeID tree = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->point_in_volume(tree, point, direction, exclude_primitives);
}

MeshID XDG::find_volume(const Position& point,
                                                   const Direction& direction) const
{
  MeshID ipc = mesh_manager()->implicit_complement();
  for (auto volume_scene_pair : volume_to_surface_tree_map_) {
    MeshID volume = volume_scene_pair.first;
    if (volume == ipc) continue;
    TreeID scene = volume_scene_pair.second;
    if (ray_tracing_interface()->point_in_volume(scene, point, &direction)) {
      return volume;
    }
  }

  // if the point could not be found in any volume, it is by definition in the implicit complement
  return ipc;
}

MeshID XDG::find_element(const Position& point) const
{
  return ray_tracing_interface()->find_element(point);
}

MeshID XDG::find_element(MeshID volume,
                         const Position& point) const
{
  TreeID scene = volume_to_point_location_tree_map_.at(volume);
  return ray_tracing_interface()->find_element(scene, point);
}

std::vector<std::pair<MeshID, double>>
XDG::segments(const Position& start,
              const Position& end) const
{
  MeshID ipc = mesh_manager()->implicit_complement();

  std::vector<MeshID> prev_elements;

  Position r = start;
  Direction u = end - start;
  double distance = u.length();
  u /= distance;

  std::vector<xdg::MeshID> hit_primitives;

  std::vector<std::pair<MeshID, double>> segments;
  while (distance > 0) {
    // attempt to find an element at the start location
    MeshID current_element = ray_tracing_interface()->find_element(r);
    // at this point we may be on the face of an element, if we're declared inside that element, ignore it
    if (segments.size() > 0 && current_element == segments.back().first) current_element = ID_NONE;
    MeshID volume = ID_NONE;
    if (current_element == ID_NONE) {
      // fire a ray against the implicit complement
      auto hit = ray_fire(ipc, r + u * TINY_BIT, u, INFTY, HitOrientation::EXITING, &hit_primitives);
      // if there is no entry point or the distance to the surface
      // is past the end point, return
      if (hit.second == ID_NONE || hit.first > distance) return segments;

      double hit_dist = hit.first + TINY_BIT;
      // move up to the surface
      r += u * hit_dist;
      distance -= hit_dist;
      // determine the volume we're moving into
      mesh_manager()->next_volume(ipc, hit.second);

      // Get the element on the other side of the hit face using adjacencies
      auto adjacent_element = mesh_manager()->get_boundary_face_element(hit_primitives.back());
      if (adjacent_element == ID_NONE) {
        warning(fmt::format("Ray fire hit surface {}, but no adjacent elements were found on the other side of the surface.", hit.second));
        return segments;
      }
      current_element = adjacent_element;
      hit_primitives.clear();
    }
    auto vol_segments = mesh_manager()->walk_elements(current_element, r, u, distance);
    // add to current set of segments
    segments.insert(segments.end(), vol_segments.begin(), vol_segments.end());
    double segment_sum = std::accumulate(vol_segments.begin(), vol_segments.end(), 0.0,
                                         [](double total, const auto& segment) { return total + segment.second; });
    // upate location of the track start
    r += u * segment_sum;
    // decrement distance by total distance traveled in the volume
    distance -= segment_sum;
  }
  return segments;
}

std::vector<std::pair<MeshID, double>>
XDG::segments(MeshID volume,
              const Position& start,
              const Position& end) const
{
  Direction u = end - start;
  double remaining_distance = u.length();
  u.normalize();

  Position r = start;
  TreeID volume_tree = volume_to_point_location_tree_map_.at(volume);
  MeshID starting_element = ray_tracing_interface()->find_element(volume_tree, r);

  std::vector<std::pair<MeshID, double>> segments;
  std::vector<MeshID> hit_primitives;

  // Loop over the ray until the remaining distance is exhausted.
  while (remaining_distance > 0.0) {
    // If we're outside of the volume of interest, find the next entry point into the volume.
    // this handles both outside the volume and inside an interior shell volume/void
    if (starting_element == ID_NONE) {
      auto entry = ray_fire(volume, r, u, remaining_distance, HitOrientation::ENTERING, &hit_primitives);
      if (entry.second == ID_NONE) break;

      r += u * entry.first;
      remaining_distance -= entry.first;
      if (remaining_distance <= 0.0) break;

      // TODO: use mesh adjacencies to find the element on the other
      // side of the hit face.
      const double bump = std::min(TINY_BIT, remaining_distance * 0.5);

      starting_element = ray_tracing_interface()->find_element(volume_tree, r + u * bump);

      if (starting_element == ID_NONE) {
        warning("Ray fire hit surface {}, but could not find element on the other side of the surface.", entry.second);
        break;
      }
    }

    // Bound this element walk by the next exit from the requested volume.
    auto exit = ray_fire(volume, r, u, remaining_distance, HitOrientation::EXITING, &hit_primitives);

    const bool exits_before_end = exit.second != ID_NONE && exit.first < remaining_distance;
    double interval_distance = remaining_distance;
    if (exits_before_end) interval_distance = exit.first;

    // Stop just short of a volume boundary so roundoff between the surface
    // and element intersection calculations cannot step into the next volume.
    double walk_distance = interval_distance;
    double boundary_remainder = 0.0;
    if (exits_before_end) {
      boundary_remainder = std::min(TINY_BIT, interval_distance * 0.5);
      walk_distance -= boundary_remainder;
    }

    auto interval = mesh_manager()->walk_elements(starting_element, r, u, walk_distance);
    if (boundary_remainder > 0.0 && !interval.empty()) {
      interval.back().second += boundary_remainder;
    }
    segments.insert(segments.end(), interval.begin(), interval.end());

    r += u * interval_distance;
    remaining_distance -= interval_distance;

    if (!exits_before_end) break;

    // After an exit, restart the loop searching for a possible re-entry.
    starting_element = ID_NONE;
  }

  return segments;
}

std::pair<MeshID, double>
XDG::next_element(MeshID current_element,
                  const Position& r,
                  const Direction& u) const
{
  return mesh_manager()->next_element(current_element, r, u);
}

std::pair<double, MeshID>
XDG::ray_fire(MeshID volume,
              const Position& origin,
              const Direction& direction,
              const double dist_limit,
              HitOrientation orientation,
              std::vector<MeshID>* const exclude_primitives) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->ray_fire(scene, origin, direction, dist_limit, orientation, exclude_primitives);
}

std::pair<double, MeshID> XDG::closest(MeshID volume,
                                       const Position& origin) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->closest(scene, origin);
}

double XDG::closest_distance(MeshID volume,
                             const Position& origin) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->closest(scene, origin).first;
}

bool XDG::occluded(MeshID volume,
              const Position& origin,
              const Direction& direction,
              double& dist) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->occluded(scene, origin, direction, dist);
}

Direction XDG::surface_normal(MeshID surface,
                              Position point,
                              const std::vector<MeshID>* exclude_primitives) const
{
  MeshID element;
  if (exclude_primitives != nullptr && exclude_primitives->size() > 0) {
    element = exclude_primitives->back();
  } else {
    auto surface_vols = mesh_manager()->get_parent_volumes(surface);
    TreeID scene = volume_to_surface_tree_map_.at(surface_vols.first);
    element = ray_tracing_interface()->closest(scene, point).second;

    // TODO: bring this back when we have a better way to handle this
    // if (geom_data.surface_id != surface) {
    //   fatal_error("Point {} was closest to surface {}, not surface {}, in volume {}.", point, geom_data.surface_id, surface, surface_vols.first);
    // }
  }
  return mesh_manager()->face_normal(element);
}

double XDG::measure_volume(MeshID volume) const
{
  double volume_total {0.0};

  auto surfaces = mesh_manager()->get_volume_surfaces(volume);

  std::vector<Sense> surface_senses;
  for (auto surface : surfaces) {
    surface_senses.push_back(mesh_manager()->surface_sense(surface, volume));
  }

  for (int i = 0; i < surfaces.size(); ++i) {
    MeshID& surface = surfaces[i];
    double surface_contribution {0.0};
    auto triangles = mesh_manager()->get_surface_faces(surface);
    for (auto triangle : triangles) {
      auto face_vertices = mesh_manager()->face_vertex_coordinates(triangle);
      surface_contribution += face_volume_contribution_from_vertices(face_vertices);
    }
    if (surface_senses[i] == Sense::REVERSE) surface_contribution *= -1.0;
    volume_total += surface_contribution;
  }

  return volume_total / 6.0;
}

double XDG::measure_surface_area(MeshID surface) const
{
  double area {0.0};
  for (auto triangle : mesh_manager()->get_surface_faces(surface)) {
    auto face_vertices = mesh_manager()->face_vertex_coordinates(triangle);
    area += face_area_from_vertices(face_vertices);
  }
  return area;
}

double XDG::measure_volume_area(MeshID volume) const
{
  double area {0.0};
  for (auto surface : mesh_manager()->get_volume_surfaces(volume)) {
    area += measure_surface_area(surface);
  }
  return area;
}

} // namespace xdg
