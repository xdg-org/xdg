#include "xdg/mesh_manager_interface.h"

#include <array>
#include <cmath>
#include <set>

#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/geometry/plucker.h"
#include "xdg/geometry/measure.h"
#include "xdg/geometry/face_common.h"
#include "xdg/element_face_accessor.h"
#include "xdg/error.h"
#include "xdg/geometry/face_common.h"
#include "xdg/geometry/plucker.h"

namespace xdg {

MeshManager::MeshManager() {
  if (XDGConfig::config().initialized() == false) {
    XDGConfig::config().initialize();
  }
}

MeshID MeshManager::create_implicit_complement() {
  // create a new volume
  MeshID ipc_volume = this->create_volume();

  for (auto surface : this->surfaces()) {
    auto parent_vols = this->get_parent_volumes(surface);

    if (parent_vols.first == ID_NONE)
      this->add_surface_to_volume(ipc_volume, surface, Sense::FORWARD);

    if (parent_vols.second == ID_NONE)
      this->add_surface_to_volume(ipc_volume, surface, Sense::REVERSE);
  }

  // insert the ipc volume into volume sets if it isn't present already
  if (std::find(volumes().begin(), volumes().end(), ipc_volume) ==
      volumes().end())
    volumes().push_back(ipc_volume);

  // TODO: allow for alternate material assignment in IPC
  volume_metadata_[{ipc_volume, PropertyType::MATERIAL}] = VOID_MATERIAL;

  implicit_complement_ = ipc_volume;

  return ipc_volume;
}

MeshID MeshManager::next_volume_id() const {
  if (volumes().empty())
    return 1;
  return *std::max_element(volumes().begin(), volumes().end()) + 1;
}

MeshID MeshManager::next_surface_id() const {
  if (surfaces().empty())
    return 1;
  return *std::max_element(surfaces().begin(), surfaces().end()) + 1;
}

bool MeshManager::volume_has_property(MeshID volume, PropertyType type) const {
  return volume_metadata_.count({volume, type}) > 0;
}

int MeshManager::num_volume_elements() const {
  int n_elements = 0;
  for (auto volume : volumes()) {
    n_elements += num_volume_elements(volume);
  }
  return n_elements;
}

std::vector<MeshID> MeshManager::get_volume_faces(MeshID volume) const {
  std::set<MeshID> elements;
  for (auto surface : this->get_volume_surfaces(volume)) {
    auto surface_elements = this->get_surface_faces(surface);
    elements.insert(surface_elements.begin(), surface_elements.end());
  }
  return std::vector<MeshID>(elements.begin(), elements.end());
}

bool MeshManager::surface_has_property(MeshID surface,
                                       PropertyType type) const {
  return surface_metadata_.count({surface, type}) > 0;
}

Property MeshManager::get_volume_property(MeshID volume,
                                          PropertyType type) const {
  return volume_metadata_.at({volume, type});
}

Property MeshManager::get_surface_property(MeshID surface,
                                           PropertyType type) const {
  if (surface_metadata_.count({surface, type}) == 0)
    return {PropertyType::BOUNDARY_CONDITION, "transmission"};
  return surface_metadata_.at({surface, type});
}

std::vector<std::pair<MeshID, double>>
MeshManager::walk_elements(MeshID starting_element, const Position &start,
                           const Direction &u, double distance) const {
  // a copy of the start position that will be updated as elements are traversed
  Position r = start;
  std::vector<std::pair<MeshID, double>> result;

  MeshID elem = starting_element;
  MeshID previous_element = ID_NONE;
  while (distance > 0) {
    // find the exit point from the current element and determine the next
    // element if one exists
    auto exit = next_element(elem, r, u);
    if (exit.second == INFTY && distance > 0) {
      warning(fmt::format("Unable to find finite exit point from element {}. Exiting walk_elements with {} distance unaccounted for.", elem, distance));
      break;
    }
    // ensure we are not traveling beyond the end of the ray
    exit.second = std::min(exit.second, distance);
    distance -= exit.second;
    // only add to the result if the distance is greater than 0
    result.push_back({elem, exit.second});
    r += exit.second * u;

    if (exit.second > TINY_BIT) {
      previous_element = ID_NONE;
    } else {
      previous_element = elem;
    }

    elem = exit.first;

    // if there is no next element, we're exiting the mesh
    if (elem == ID_NONE) {
      break;
    }
  }

  return result;
}

std::vector<std::pair<MeshID, double>>
MeshManager::walk_elements(MeshID starting_element, const Position &start,
                           const Position &end) const {
  Position u = (end - start);
  double distance = u.length();
  u.normalize();
  return walk_elements(starting_element, start, u, distance);
}

std::pair<MeshID, double> MeshManager::next_element(MeshID current_element,
                                                    const Position &r,
                                                    const Position &u) const {
  struct FaceCandidate {
    MeshID element {ID_NONE};
    double distance;
    int face {ID_NONE};
    double exiting_dot;
  };

  auto element_face_accessor = ElementFaceAccessor::create(this, current_element);

  constexpr int EXITING_ORIENTATION = 1;
  std::vector<FaceCandidate> candidates;

  const int num_faces = element_face_accessor->num_faces();
  for (int i = 0; i < num_faces; i++) {
    auto coords = element_face_accessor->face_vertices(i);
    const double exiting_dot = u.dot(triangle_normal(coords));

    PluckerIntersectionResult result;
    if (coords.size() == 3) {
      result = plucker_ray_tri_intersect(
          coords.data(), r, u, INFTY, -1e-10, true, EXITING_ORIENTATION);
    } else if (coords.size() == 4) {
      std::array<Vertex, 3> tri0;
      std::array<Vertex, 3> tri1;
      if (canonical_diagonal(coords)) {
        tri0 = {coords[0], coords[1], coords[2]};
        tri1 = {coords[0], coords[2], coords[3]};
      } else {
        tri0 = {coords[1], coords[2], coords[3]};
        tri1 = {coords[1], coords[3], coords[0]};
      }
      auto result0 = plucker_ray_tri_intersect(tri0.data(),
                                               r,
                                               u,
                                               INFTY,
                                               0.0,
                                               true,
                                               EXITING_ORIENTATION);
      auto result1 = plucker_ray_tri_intersect(tri1.data(),
                                               r,
                                               u,
                                               INFTY,
                                               0.0,
                                               true,
                                               EXITING_ORIENTATION);
      bool hit0 = result0.hit;
      bool hit1 = result1.hit;
      double dist0 = result0.t;
      double dist1 = result1.t;
      if (hit0 || hit1) {
        result.hit = true;
        if (hit0 && hit1) {
          result.t = std::min(dist0, dist1);
        } else {
          result.t = hit0 ? dist0 : dist1;
        }
      }
    } else {
      fatal_error("Unsupported face vertex count {} in next_element", coords.size());
    }

    if (!result.hit) continue;

    MeshID next_element = this->adjacent_element(current_element, i);

    candidates.push_back(
        {next_element, std::max(0.0, result.t), i, exiting_dot});
  }

  if (candidates.empty()) return {ID_NONE, INFTY};

  // find the minimum distance among candidate hits
  double min_dist = INFTY;
  for (const auto &candidate : candidates) {
    min_dist = std::min(min_dist, candidate.distance);
  }

  // find all candidates that are tied for the minimum distance
  // break ties by selecting the face most aligned with the query direction
  const double tie_tol = 1.0e-10 * std::max(1.0, std::abs(min_dist));
  std::vector<const FaceCandidate *> tied_candidates;
  for (const auto &candidate : candidates) {
    if (std::abs(candidate.distance - min_dist) > tie_tol)
      continue;
    tied_candidates.push_back(&candidate);
  }

  if (tied_candidates.size() == 1)
    return {tied_candidates.front()->element, tied_candidates.front()->distance};

  const auto selected = *std::max_element(
    tied_candidates.begin(), tied_candidates.end(),
    [](const auto a, const auto b) { return a->exiting_dot < b->exiting_dot; });

  return {selected->element, selected->distance};
}

MeshID MeshManager::next_volume(MeshID current_volume, MeshID surface) const {
  auto parent_vols = this->get_parent_volumes(surface);

  if (parent_vols.first == current_volume)
    return parent_vols.second;
  else if (parent_vols.second == current_volume)
    return parent_vols.first;
  else
    fatal_error("Volume {} is not a parent of surface {}", current_volume,
                surface);

  return ID_NONE;
}

Direction MeshManager::face_normal(MeshID element) const
{
  auto vertices = this->face_vertex_coordinates(element);
  return (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalize();
}

BoundingBox MeshManager::element_bounding_box(MeshID element) const {
  auto vertices = this->element_vertices(element);
  return BoundingBox::from_points(vertices);
}

BoundingBox
MeshManager::face_bounding_box(MeshID element) const
{
  auto vertices = this->face_vertex_coordinates(element);
  return BoundingBox::from_points(vertices);
}

BoundingBox MeshManager::volume_bounding_box(MeshID volume) const {
  BoundingBox bb;
  auto surfaces = this->get_volume_surfaces(volume);
  for (auto surface : surfaces) {
    bb.update(this->surface_bounding_box(surface));
  }
  return bb;
}

BoundingBox MeshManager::global_bounding_box() const {
  BoundingBox bb;
  auto volumes = this->volumes();
  for (auto volume : volumes) {
    bb.update(this->volume_bounding_box(volume));
  }
  return bb;
}

BoundingBox MeshManager::surface_bounding_box(MeshID surface) const {
  auto elements = this->get_surface_faces(surface);
  BoundingBox bb;
  for (const auto &element : elements) {
    bb.update(this->face_bounding_box(element));
  }
  return bb;
}

double MeshManager::element_volume(MeshID element) const
{
  // create an element face accessor
  auto element_face_accessor = ElementFaceAccessor::create(this, element);
  const int num_faces = element_face_accessor->num_faces();

  double volume = 0.0;
  for (int i = 0; i < num_faces; i++) {
    auto vertices = element_face_accessor->face_vertices(i);
    volume += face_volume_contribution_from_vertices(vertices);
  }
  return volume / 6.0;
}

std::vector<Vertex>
MeshManager::face_vertex_coordinates(MeshID face) const
{
  auto vertex_ids = this->face_vertices(face);
  std::vector<Vertex> vertices;
  vertices.reserve(vertex_ids.size());
  for (auto id : vertex_ids) {
    vertices.push_back(this->vertex_coordinates(id));
  }
  return vertices;
}

std::pair<MeshID, MeshID>
MeshManager::get_parent_volumes(MeshID surface) const {
  return this->surface_senses(surface);
}

MeshManager::LocalMeshData
MeshManager::surface_local_mesh_data(MeshID surface) const {
  const auto faces = get_surface_faces(surface);
  const auto connectivity_func = [this](MeshID face) {
    return face_connectivity(face);
  };

  return local_mesh_data(faces, connectivity_func);
}

MeshManager::LocalMeshData
MeshManager::volume_local_mesh_data(MeshID volume) const {
  const auto elements = get_volume_elements(volume);
  const auto connectivity_func = [this](MeshID element) {
    return element_connectivity(element);
  };

  return local_mesh_data(elements, connectivity_func);
}

std::vector<Vertex> MeshManager::get_surface_vertices(MeshID surface) const {
  return surface_local_mesh_data(surface).vertices;
}

std::vector<int> MeshManager::get_surface_connectivity(MeshID surface) const {
  return surface_local_mesh_data(surface).connectivity;
}

std::vector<Vertex> MeshManager::get_volume_vertices(MeshID volume) const {
  return volume_local_mesh_data(volume).vertices;
}

std::vector<int> MeshManager::get_volume_connectivity(MeshID volume) const {
  return volume_local_mesh_data(volume).connectivity;
}

} // namespace xdg
