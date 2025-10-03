#include "xdg/mesh_manager_interface.h"

#include <algorithm>
#include <set>

#include "xdg/error.h"
#include "xdg/geometry/plucker.h"
#include "xdg/geometry/face_common.h"
#include "xdg/element_face_accessor.h"

namespace xdg {

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

MeshID MeshManager::next_volume_id() const
{
  if (volumes().empty()) return 1;
  return *std::max_element(volumes().begin(), volumes().end()) + 1;
}

MeshID MeshManager::next_surface_id() const
{
  if (surfaces().empty()) return 1;
  return *std::max_element(surfaces().begin(), surfaces().end()) + 1;
}

bool
MeshManager::volume_has_property(MeshID volume, PropertyType type) const
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
MeshManager::surface_has_property(MeshID surface, PropertyType type) const
{
  return surface_metadata_.count({surface, type}) > 0;
}

Property
MeshManager::get_volume_property(MeshID volume, PropertyType type) const
{
  return volume_metadata_.at({volume, type});
}

Property
MeshManager::get_surface_property(MeshID surface, PropertyType type) const
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
    // find the exit point from the current element and determine the next element
    // if one exists
    auto exit = next_element(elem, r, u);
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
  std::array<double, 4> dists = {INFTY, INFTY, INFTY, INFTY};
  std::array<bool, 4> hit_types;

  auto element_face_accessor = ElementFaceAccessor::create(this, current_element);

  // get the faces (triangles) of this element
  for (int i = 0; i < 4; i++) {
    // triangle connectivity
    auto coords = element_face_accessor->face_vertices(i);

    // get the normal of the triangle face
    const Position normal = triangle_normal(coords);

    // exiting hit only, assumes triangle normals point outward
    // with respect to the element
    int orientation = 1;
    // perform ray-triangle intersection

    auto result = plucker_ray_tri_intersect(coords,
                                            r,
                                            u,
                                            INFTY,
                                            nullptr,
                                            &orientation);

    hit_types[i] = result.hit;
    // set distance and ensure it is non-negative
    dists[i] = result.hit ? std::max(0.0, result.t) : INFTY;
  }

  // determine the minimum distance to exit and the face number
  int idx_out = ID_NONE;
  double min_dist = INFTY;
  // choose the exiting face based on the minimum distance,
  // if all distances are INFTY (no hit), then the index will
  // not be updated
  for (int i = 0; i < dists.size(); i++) {
    if (dists[i] < min_dist) {
      min_dist = dists[i];
      idx_out = i;
    }
  }

  MeshID next_element = this->adjacent_element(current_element, idx_out);
  return {next_element, min_dist};
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
  auto vertices = this->face_vertices(element);
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
  auto vertices = this->face_vertices(element);
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

std::pair<MeshID, MeshID>
MeshManager::get_parent_volumes(MeshID surface) const
{
  return this->surface_senses(surface);
}

} // namespace xdg