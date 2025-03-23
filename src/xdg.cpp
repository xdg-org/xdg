#include <vector>

#include "xdg/xdg.h"
#include "xdg/error.h"
#include "xdg/embree/ray_tracer.h"
// #include "xdg/gprt/ray_tracer.h" Not implemented yet

// mesh manager concrete implementations
#ifdef XDG_ENABLE_MOAB
#include "xdg/moab/mesh_manager.h"
#endif

#ifdef XDG_ENABLE_LIBMESH
#include "xdg/libmesh/mesh_manager.h"
#endif

#include "xdg/constants.h"
#include "xdg/geometry/measure.h"
namespace xdg {

void XDG::prepare_raytracer()
{
  for (auto volume : mesh_manager()->volumes()) {
    this->prepare_volume_for_raytracing(volume);
  }
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
  };

  // Ray tracer factory dispatch
  auto rt_factory = [&]() -> std::shared_ptr<RayTracer> {
    #ifdef XDG_ENABLE_EMBREE
    if (ray_tracing_lib == RTLibrary::EMBREE) return std::make_shared<EmbreeRayTracer>();
    #endif
    #ifdef XDG_ENABLE_GPRT
    // if (ray_tracing_lib == RTLibrary::GPRT) return std::make_shared<GPRTRayTracer>();
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
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  return ray_tracing_interface()->point_in_volume(scene, point, direction, exclude_primitives);
}

MeshID XDG::find_volume(const Position& point,
                                                   const Direction& direction) const
{
  for (auto volume_scene_pair : volume_to_surface_tree_map_) {
    MeshID volume = volume_scene_pair.first;
    TreeID scene = volume_scene_pair.second;
    if (ray_tracing_interface()->point_in_volume(scene, point, &direction)) {
      return volume;
    }
  }
  return ID_NONE;
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

void XDG::closest(MeshID volume,
              const Position& origin,
              double& dist,
              MeshID& triangle) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  ray_tracing_interface()->closest(scene, origin, dist, triangle);
}

void XDG::closest(MeshID volume,
              const Position& origin,
              double& dist) const
{
  TreeID scene = volume_to_surface_tree_map_.at(volume);
  ray_tracing_interface()->closest(scene, origin, dist);
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
    double dist;
    TreeID scene = volume_to_surface_tree_map_.at(surface_vols.first);
    ray_tracing_interface()->closest(scene, point, dist, element);

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
      surface_contribution += triangle_volume_contribution(mesh_manager()->face_vertices(triangle));
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
    area += triangle_area(mesh_manager()->face_vertices(triangle));
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