#include "xdg/mesh_manager_interface.h"

#include <array>
#include <cmath>
#include <set>

#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/fmt_formatters.h"
#include "xdg/error.h"
#include "xdg/geometry/face_common.h"
#include "xdg/geometry/measure.h"
#include "xdg/geometry/plucker.h"
#include "xdg/element_face_accessor.h"
#include "xdg/geometry/element_face_intersect.h"

namespace xdg {

MeshManager::MeshManager() {
  if (XDGConfig::config().initialized() == false) {
    XDGConfig::config().initialize();
  }
}

MeshID
MeshManager::create_implicit_complement()
{
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
  if (std::find(volumes().begin(), volumes().end(), ipc_volume) == volumes().end())
    volumes().push_back(ipc_volume);

  // TODO: allow for alternate material assignment in IPC
  volume_metadata_[{ipc_volume, PropertyType::MATERIAL}] = VOID_MATERIAL;

  implicit_complement_ = ipc_volume;

  return ipc_volume;
}

MeshID
MeshManager::next_volume_id() const {
  if (volumes().empty()) return 1;
  return *std::max_element(volumes().begin(), volumes().end()) + 1;
}

MeshID
MeshManager::next_surface_id() const {
  if (surfaces().empty()) return 1;
  return *std::max_element(surfaces().begin(), surfaces().end()) + 1;
}

bool
MeshManager::volume_has_property(MeshID volume,
                                 PropertyType type) const
{
  return volume_metadata_.count({volume, type}) > 0;
}

int
MeshManager::num_volume_elements() const
{
  int n_elements = 0;
  for (auto volume : volumes()) {
    n_elements += num_volume_elements(volume);
  }
  return n_elements;
}

int
MeshManager::num_volume_element_faces(MeshID volume) const
{
  auto num_elem = num_volume_elements(volume);
  if (num_elem == 0) return 0;

  auto elem_type = get_volume_element_type(volume);
  switch (elem_type) {
    case VolumeElementType::TET: return 4 * num_elem;
    case VolumeElementType::HEX: return 6 * num_elem;
    default: {
      fatal_error("Unsupported element type in 'num_volume_element_faces' {}", elem_type);
      return 0;
    }
  }
}

int
MeshManager::num_volume_element_faces() const
{
  int n_element_faces = 0;
  for (auto volume : volumes()) {
    if (implicit_complement() == volume) continue;
    n_element_faces += num_volume_element_faces(volume);
  }
  return n_element_faces;
}

std::vector<MeshID>
MeshManager::get_volume_faces(MeshID volume) const
{
  std::set<MeshID> elements;
  for (auto surface : this->get_volume_surfaces(volume)) {
    auto surface_elements = this->get_surface_faces(surface);
    elements.insert(surface_elements.begin(), surface_elements.end());
  }
  return std::vector<MeshID>(elements.begin(), elements.end());
}

bool
MeshManager::surface_has_property(MeshID surface,
                                  PropertyType type) const
{
  return surface_metadata_.count({surface, type}) > 0;
}

Property
MeshManager::get_volume_property(MeshID volume,
                                 PropertyType type) const
{
  return volume_metadata_.at({volume, type});
}

Property
MeshManager::get_surface_property(MeshID surface,
                                  PropertyType type) const
{
  if (surface_metadata_.count({surface, type}) == 0)
    return {PropertyType::BOUNDARY_CONDITION, "transmission"};
  return surface_metadata_.at({surface, type});
}

std::vector<std::pair<MeshID, double>>
MeshManager::walk_elements(MeshID starting_element,
                           const Position& start,
                           const Direction& u,
                           double distance) const
{
  // a copy of the start position that will be updated as elements are traversed
  Position r = start;
  std::vector<std::pair<MeshID, double>> result;

  MeshID elem = starting_element;
  while (distance > 0) {
    // find the exit point from the current element and determine the next
    // element if one exists
    auto exit = next_element(elem, r, u);
    if (exit.second == INFTY && distance > 0) {
      // TODO: Update to fatal_error once configurable error handling is implemented
      warning(fmt::format("Unable to find finite exit point from element {}. Exiting walk_elements with {} distance unaccounted for.", elem, distance));
      break;
    }
    // ensure we are not traveling beyond the end of the ray
    exit.second = std::min(exit.second, distance);
    distance -= exit.second;
    // only add to the result if the distance is greater than 0
    result.push_back({elem, exit.second});
    r += exit.second * u;

    elem = exit.first;

    // if there is no next element, we're exiting the mesh
    if (elem == ID_NONE) {
      break;
    }
  }

  return result;
}

std::vector<std::pair<MeshID, double>>
MeshManager::walk_elements(MeshID starting_element,
                           const Position& start,
                           const Position& end) const
{
  Position u = (end - start);
  double distance = u.length();
  u.normalize();
  return walk_elements(starting_element, start, u, distance);
}

std::pair<MeshID, double>
MeshManager::next_element(MeshID current_element,
                          const Position& r,
                          const Position& u) const
{
  struct FaceCandidate {
    MeshID element {ID_NONE};
    double distance;
    double exiting_dot;
  };
  std::vector<FaceCandidate> candidates;

  const auto element_face_accessor = ElementFaceAccessor::create(this, current_element);
  const int num_faces = element_face_accessor->num_faces();
  for (int i = 0; i < num_faces; i++) {
    // Intersect the face. If no intersection is recorded, skip checking subfaces.
    const auto result = intersect_elem_face(r, u, *element_face_accessor, i, false);
    if (!result.hit) continue;

    // If the element has subfaces we need to figure out which face is composed of
    // multiple subfaces. Then, we resolve which subface we're entering into.
    if (element_face_accessor->has_sub_faces()) {
      const int num_subfaces = element_face_accessor->num_sub_faces(i);
      // This face isn't split, so we can just push back into the candidates vector.
      if (num_subfaces == 1) {
        candidates.push_back({this->adjacent_element(current_element, i),
                              std::max(0.0, result.distance),
                              result.exiting_dot});
      } else {
        // This face has subfaces.
        for (int sf = 0; sf < num_subfaces; ++sf) {
          const auto [subelement, adjacent_face] = this->adjacent_subelement(current_element, i, sf);
          const auto subelement_face_accessor = ElementFaceAccessor::create(this, subelement);
          const auto sub_result = intersect_elem_face(r, u, *subelement_face_accessor, adjacent_face, true);
          if (sub_result.hit) {
            candidates.push_back({subelement, 
                                  std::max(0.0, sub_result.distance), 
                                  sub_result.exiting_dot});
          }
        }
      }
    } else {
      // If there are no subfaces, we can just push back the adjacent element.
      candidates.push_back({this->adjacent_element(current_element, i),
                            std::max(0.0, result.distance),
                            result.exiting_dot});
    }
  }

  if (candidates.empty()) return {ID_NONE, INFTY};

  if (candidates.size() == 1) return {candidates.front().element, candidates.front().distance};

  // find the minimum distance among candidate hits
  double min_dist = INFTY;
  for (const auto &candidate : candidates) {
    min_dist = std::min(min_dist, candidate.distance);
  }

  // find all candidates that are tied for the minimum distance
  // break ties by selecting the face most aligned with the query direction
  const double tie_tol = TINY_BIT * std::max(1.0, std::abs(min_dist));
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

MeshID MeshManager::next_volume(MeshID current_volume, MeshID surface) const
{
  auto parent_vols = this->get_parent_volumes(surface);

  if (parent_vols.first == current_volume)
    return parent_vols.second;
  else if (parent_vols.second == current_volume)
    return parent_vols.first;
  else
    fatal_error("Volume {} is not a parent of surface {}", current_volume, surface);

  return ID_NONE;
}

Direction MeshManager::face_normal(MeshID element) const
{
  auto vertices = this->face_vertex_coordinates(element);
  return (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).normalize();
}

BoundingBox
MeshManager::element_bounding_box(MeshID element) const
{
  auto vertices = this->element_vertices(element);
  return BoundingBox::from_points(vertices);
}

BoundingBox
MeshManager::face_bounding_box(MeshID element) const
{
  auto vertices = this->face_vertex_coordinates(element);
  return BoundingBox::from_points(vertices);
}

BoundingBox
MeshManager::volume_bounding_box(MeshID volume) const
{
  BoundingBox bb;
  auto surfaces = this->get_volume_surfaces(volume);
  for (auto surface : surfaces) {
    bb.update(this->surface_bounding_box(surface));
  }
  return bb;
}

BoundingBox
MeshManager::global_bounding_box() const
{
  BoundingBox bb;
  auto volumes = this->volumes();
  for (auto volume : volumes) {
    bb.update(this->volume_bounding_box(volume));
  }
  return bb;
}

BoundingBox
MeshManager::surface_bounding_box(MeshID surface) const
{
  auto elements = this->get_surface_faces(surface);
  BoundingBox bb;
  for (const auto& element : elements) {
    bb.update(this->face_bounding_box(element));
  }
  return bb;
}

std::pair<MeshID, int> 
MeshManager::adjacent_subelement(MeshID element, int face, int subface_idx) const
{
  fatal_error("'adjacent_subelement' has not been implemented for this mesh manager!");
  return {ID_NONE, ID_NONE};
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
MeshManager::get_parent_volumes(MeshID surface) const
{
  return this->surface_senses(surface);
}

MeshManager::LocalMeshData
MeshManager::surface_local_mesh_data(MeshID surface) const
{
  const auto faces = get_surface_faces(surface);
  const auto connectivity_func = [this](MeshID face) {
    return face_connectivity(face);
  };

  return local_mesh_data(faces, connectivity_func);
}

MeshManager::LocalMeshData
MeshManager::volume_local_mesh_data(MeshID volume) const
{
  const auto elements = get_volume_elements(volume);
  const auto connectivity_func = [this](MeshID element) {
    return element_connectivity(element);
  };

  return local_mesh_data(elements, connectivity_func);
}

std::vector<Vertex> MeshManager::get_surface_vertices(MeshID surface) const
{
  return surface_local_mesh_data(surface).vertices;
}

std::vector<int> MeshManager::get_surface_connectivity(MeshID surface) const
{
  return surface_local_mesh_data(surface).connectivity;
}

std::vector<Vertex> MeshManager::get_volume_vertices(MeshID volume) const
{
  return volume_local_mesh_data(volume).vertices;
}

std::vector<int> MeshManager::get_volume_connectivity(MeshID volume) const
{
  return volume_local_mesh_data(volume).connectivity;
}

} // namespace xdg
