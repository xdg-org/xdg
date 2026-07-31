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

bool plucker_tet_containment_test(const Position &point, const Vertex &v0,
                                  const Vertex &v1, const Vertex &v2,
                                  const Vertex &v3);

namespace {

struct ElementExit {
  MeshID element{ID_NONE};
  double distance{INFTY};
  int face{ID_NONE};
};

Direction outward_normal(const std::array<Vertex, 3> &coords,
                         const Position &centroid, MeshID element, int face) {
  Direction normal = triangle_normal(coords);
  const Position face_centroid = (coords[0] + coords[1] + coords[2]) / 3.0;
  const Position face_to_centroid = centroid - face_centroid;
  if (normal.dot(face_to_centroid) > 0.0) {
    warning("Face {} of element {} has an inward-pointing normal.", face,
            element);
    normal = -normal;
  }
  return normal;
}

ElementExit find_element_exit(const MeshManager &mesh_manager,
                              MeshID current_element, const Position &r,
                              const Direction &u, MeshID excluded_element) {
  struct FaceCandidate {
    MeshID element;
    double distance;
    int face;
    double exiting_dot;
    bool contains_probe;
  };

  std::vector<FaceCandidate> candidates;
  auto element_face_accessor =
      ElementFaceAccessor::create(&mesh_manager, current_element);
  auto current_element_vertices =
      mesh_manager.element_vertices(current_element);
  Position centroid{0.0, 0.0, 0.0};
  for (const auto &vertex : current_element_vertices) {
    centroid += vertex;
  }
  centroid /= static_cast<double>(current_element_vertices.size());

  constexpr double EXIT_DOT_TOL = 1.0e-14;
  for (int i = 0; i < 4; i++) {
    auto coords = element_face_accessor->face_vertices(i);
    const Direction normal =
        outward_normal(coords, centroid, current_element, i);
    const double exiting_dot = u.dot(normal);
    if (exiting_dot <= EXIT_DOT_TOL)
      continue;

    auto result =
        plucker_ray_tri_intersect(coords.data(), r, u, INFTY, -1e-10, false, 0);
    if (!result.hit)
      continue;

    MeshID next_element = mesh_manager.adjacent_element(current_element, i);
    if (next_element != ID_NONE && next_element == excluded_element)
      continue;

    bool contains_probe = false;
    if (next_element != ID_NONE) {
      const Position probe = r + (std::max(0.0, result.t) + TINY_BIT) * u;
      auto next_vertices = mesh_manager.element_vertices(next_element);
      contains_probe = plucker_tet_containment_test(probe, next_vertices[0],
          next_vertices[1], next_vertices[2], next_vertices[3]);
    }

    candidates.push_back(
        {next_element, std::max(0.0, result.t), i, exiting_dot,
            contains_probe});
  }

  if (candidates.empty())
    return {};

  double min_dist = INFTY;
  for (const auto &candidate : candidates) {
    min_dist = std::min(min_dist, candidate.distance);
  }

  const double tie_tol = 1.0e-10 * std::max(1.0, std::abs(min_dist));
  const FaceCandidate *selected = nullptr;
  for (const auto &candidate : candidates) {
    if (std::abs(candidate.distance - min_dist) > tie_tol)
      continue;

    if (selected == nullptr) {
      selected = &candidate;
      continue;
    }

    const bool selected_is_boundary = selected->element == ID_NONE;
    const bool candidate_is_boundary = candidate.element == ID_NONE;
    if (selected->contains_probe != candidate.contains_probe) {
      if (candidate.contains_probe)
        selected = &candidate;
      continue;
    }

    if (selected_is_boundary != candidate_is_boundary) {
      if (!candidate_is_boundary)
        selected = &candidate;
      continue;
    }

    if (candidate.exiting_dot > selected->exiting_dot)
      selected = &candidate;
  }

  return {selected->element, selected->distance, selected->face};
}

} // namespace

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
  MeshID previous_element_at_point = ID_NONE;
  int zero_distance_crossings = 0;
  constexpr int MAX_ZERO_DISTANCE_CROSSINGS = 32;
  while (distance > 0) {
    // find the exit point from the current element and determine the next
    // element if one exists
    auto exit = find_element_exit(*this, elem, r, u, previous_element_at_point);
    // ensure we are not traveling beyond the end of the ray
    exit.distance = std::min(exit.distance, distance);
    distance -= exit.distance;
    // only add to the result if the distance is greater than 0
    result.push_back({elem, exit.distance});
    r += exit.distance * u;

    if (exit.distance > TINY_BIT) {
      previous_element_at_point = ID_NONE;
      zero_distance_crossings = 0;
    } else {
      previous_element_at_point = elem;
      ++zero_distance_crossings;
      if (zero_distance_crossings > MAX_ZERO_DISTANCE_CROSSINGS) {
        warning("Exceeded {} zero-distance element crossings while walking "
                "elements from ({}, {}, {}) in direction ({}, {}, {}).",
                MAX_ZERO_DISTANCE_CROSSINGS, start[0], start[1], start[2], u[0],
                u[1], u[2]);
        break;
      }
    }

    elem = exit.element;

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
  auto exit = find_element_exit(*this, current_element, r, u, ID_NONE);
  return {exit.element, exit.distance};
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
