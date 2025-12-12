#include "xdg/libmesh/mesh_manager.h"

#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/geometry/plucker.h"
#include "xdg/geometry/face_common.h"
#include "xdg/util/str_utils.h"

#include "libmesh/boundary_info.h"
#include "libmesh/elem.h"
#include "libmesh/mesh_base.h"
#include "libmesh/mesh_tools.h"

namespace xdg {

// Constructors
LibMeshManager::LibMeshManager(void *ptr) {}

LibMeshManager::LibMeshManager() : MeshManager() {}

void LibMeshManager::load_file(const std::string &filepath) {
  mesh_ = std::make_unique<libMesh::Mesh>(*XDGConfig::config().libmesh_comm(), 3);
  mesh_->read(filepath);
}

void LibMeshManager::init() {
  // ensure that the mesh is 3-dimensional, for our use case this is expected
  if (mesh_->mesh_dimension() != 3) {
    fatal_error("Mesh must be 3-dimensional");
  }

  num_elements_ = mesh()->n_active_elem();

  auto libmesh_bounding_box = libMesh::MeshTools::create_bounding_box(*mesh());

  // identify all subdomain IDs in the mesh, these represent volumes
  std::set<libMesh::subdomain_id_type> subdomain_ids;
  mesh()->subdomain_ids(subdomain_ids);
  for (auto id : subdomain_ids) {
    volumes_.push_back(id);
  }

  // identify all sideset IDs in the mesh, these represent surfaces
  std::set<MeshID> boundary_ids;
  auto boundary_info = mesh()->get_boundary_info();
  for (auto entry : boundary_info.get_sideset_name_map()) {
    boundary_ids.insert(entry.first);
  }

  // invert the boundary info sideset map so that we can identify
  // the elements associated with each sideset
  for (auto entry : boundary_info.get_sideset_map()) {
    const libMesh::Elem* other_elem = entry.first->neighbor_ptr(entry.second.first);
    sideset_face_map_[entry.second.second].push_back(sidepair_id({entry.first, entry.second.first}));
  }

  // search for any implicit sidesets (faces that are the boundary between two
  // subdomains/volumes)
  discover_surface_elements();

  // merge sidesets into interfaces where possible
  merge_sidesets_into_interfaces();

  // create surfaces from sidesets and interfaces
  create_surfaces_from_sidesets_and_interfaces();

  // determine the senses of the surfaces, defines
  // the mesh-based topology of the geometry
  determine_surface_senses();

  // create a sideset for all faces on the boundary of the mesh
  create_boundary_sideset();

  // create an implicit complement
  create_implicit_complement();

  // libMesh initialization
  mesh()->prepare_for_use();

  map_id_spaces();
}

MeshID LibMeshManager::adjacent_element(MeshID element, int face) const {
  const auto elem_ptr = mesh()->elem_ptr(element);
  if (!elem_ptr) return ID_NONE;
  auto neighbor = elem_ptr->neighbor_ptr(face);
  if (!neighbor) return ID_NONE;
  return neighbor->id();
}

double
LibMeshManager::element_volume(MeshID element) const {
  const auto elem_ptr = mesh()->elem_ptr(element);
  if (!elem_ptr) {
    fatal_error("Invalid element ID in element_volume");
  }
  return elem_ptr->volume();
}

MeshID LibMeshManager::create_volume() {
  MeshID next_volume_id = *std::max_element(volumes_.begin(), volumes_.end()) + 1;
  return next_volume_id;
}

void LibMeshManager::add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite) {
    auto senses = surface_senses(surface);
    if (sense == Sense::FORWARD) {
      if (!overwrite && senses.first != ID_NONE) {
        fatal_error("Surface already has a forward sense");
      }
      surface_senses_[surface] = {volume, senses.second};
    } else {
      if (!overwrite && senses.second != ID_NONE) {
        fatal_error("Surface already has a reverse sense");
      }
      surface_senses_[surface] = {senses.first, volume};
    }
}

void LibMeshManager::parse_metadata() {
  // surface metadata
  auto boundary_info = mesh()->get_boundary_info();
  auto sideset_name_map = boundary_info.get_sideset_name_map();
  for (auto surface : surfaces_) {
    if (sideset_name_map.find(surface) != sideset_name_map.end()) {
      std::string sideset_name = sideset_name_map[surface];
      remove_substring(sideset_name, "boundary:");
      surface_metadata_[{surface, PropertyType::BOUNDARY_CONDITION}] = {
          PropertyType::BOUNDARY_CONDITION, sideset_name};
    }
  }

  // volume metadata
  for (auto volume : volumes_) {
    std::string subdomain_name = mesh()->subdomain_name(volume);
    remove_substring(subdomain_name, "mat:");
    if (subdomain_name.empty()) {
      volume_metadata_[{volume, PropertyType::MATERIAL}] = VOID_MATERIAL;
    } else {
      volume_metadata_[{volume, PropertyType::MATERIAL}] = {PropertyType::MATERIAL, subdomain_name};
    }
  }
}

template<typename T>
bool intersects_set(const std::set<T>& set1, const std::set<T>& set2) {
  for (const auto& elem : set2) {
    if (set1.count(elem) > 0) {
      return true;
    }
  }
  return false;
}

template<typename T>
bool contains_set(const std::set<T>& set1, const std::set<T>& set2) {
  for (const auto& elem : set2) {
    if (set1.count(elem) == 0) {
      return false;
    }
  }
  return true;
}

void LibMeshManager::map_id_spaces() {
  // build the BlockMapping for volume elements
  std::vector<MeshID> volume_element_ids;
  volume_element_ids.reserve(mesh()->n_active_elem());
  for (const auto *elem : mesh()->active_element_ptr_range()) {
    volume_element_ids.push_back(elem->id());
  }
  volume_element_id_map_ = BlockMapping<MeshID>(volume_element_ids);

  // build the BlockMapping for vertex IDs
  std::vector<MeshID> vertex_ids;
  vertex_ids.reserve(mesh()->n_nodes());
  for (const auto *node : mesh()->node_ptr_range()) {
    vertex_ids.push_back(node->id());
  }
  vertex_id_map_ = BlockMapping<MeshID>(vertex_ids);
}

void LibMeshManager::discover_surface_elements() {
  // as part of this process, we will also build a vector of all
  // volumetric element IDs
  std::vector<MeshID> volume_element_ids;
  volume_element_ids.reserve(mesh()->n_active_elem());

  subdomain_interface_map_.clear();
  // for any active local elements, identify element faces
  // where the subdomain IDs are different on either side
  for (const auto *elem : mesh()->active_element_ptr_range()) {
    volume_element_ids.push_back(elem->id());
    MeshID subdomain_id = elem->subdomain_id();
    for (int i = 0; i < elem->n_sides(); i++) {
      auto neighbor = elem->neighbor_ptr(i);
      // get the subdomain ID of the neighbor, if it exists
      // otherwise set to ID_NONE
      MeshID neighbor_id = neighbor ? neighbor->subdomain_id() : ID_NONE;
      // if these IDs are different, then this is an interface element
      if (neighbor_id == subdomain_id) continue;
      // ensure that there is only one interface between each block pair
      if (subdomain_interface_map_.count({neighbor_id, subdomain_id}) != 0) {
        subdomain_interface_map_[{neighbor_id, subdomain_id}].insert(sidepair_id({elem, i}));
      } else {
        subdomain_interface_map_[{subdomain_id, neighbor_id}].insert(sidepair_id({elem, i}));
      }
    }
  }

  // build the BlockMapping for volume elements
  volume_element_id_map_ = BlockMapping<MeshID>(volume_element_ids);
}

void LibMeshManager::merge_sidesets_into_interfaces() {
  // replace implicit interface surfaces with sideset surfaces where needed
  // this is done by identifying the subdomain IDs for each sideset and
  // replacing the interface elements with the sideset elements.
  // Partial replacement is allowed. If any elements remain in the
  // interface sets after this operation, they will be treated as interfaces
  // between subdomains
  for (const auto& [sideset_id, sideset_elems] : sideset_face_map_) {
    if (sideset_elems.size() == 0) continue;

    // determine the subdomain IDs for the sideset
    // (possible that the face is the boundary of the mesh)
    std::pair<MeshID, MeshID> subdomain_pair {ID_NONE, ID_NONE};
    auto elem_pair = sidepair(sideset_elems.at(0));
    subdomain_pair.first = elem_pair.first()->subdomain_id();
    auto neighbor = elem_pair.second();
    // set to ID_NONE if the neighbor is null
    subdomain_pair.second = neighbor ? neighbor->subdomain_id() : ID_NONE;

    // if this is a defined sideset, it should match one of the pairs in the
    // interface map. If it doesn't based on the current ordering of subdomains,
    // swap the order
    if(subdomain_interface_map_.count(subdomain_pair) == 0) {
      subdomain_pair = {subdomain_pair.second, subdomain_pair.first};
    }

    // if the sideset pair doesn't exist in the interface map at all,
    // then we have a problem or a poorly defined (or inconsistent) sideset
    if (subdomain_interface_map_.count(subdomain_pair) == 0) {
      fatal_error("No interface elements found for sideset");
    }

    // replace the interface elements with the sideset elements
    std::set<MeshID>& interface_set = subdomain_interface_map_[subdomain_pair];

    // remove the explicit sideset elements from the discovered
    // interface elements that match this subdomain pair
    for (const auto& elem : sideset_elems) {
      interface_set.erase(elem);
    }
  }
}

void LibMeshManager::create_surfaces_from_sidesets_and_interfaces() {
  // start by creating surfaces for each sideset. These have explicit IDs
  // and may be used to define boundary conditions.
  for (const auto& [sideset_id, sideset_elems] : sideset_face_map_) {
    surfaces().push_back(sideset_id);
    for (const auto& elem : sideset_elems) {
      surface_map_[sideset_id].push_back(elem);
    }
  }

  MeshID next_surface_id = surfaces().size() == 0 ? 1 : *std::max_element(surfaces().begin(), surfaces().end()) + 1;

  std::set<std::pair<MeshID, MeshID>> visited_interfaces;
  for (auto &[pair, elements] : subdomain_interface_map_) {

    if (elements.size() == 0) {
      std::cout << "No elements found for interface between " << pair.first << " and " << pair.second << std::endl;
      continue;
    }
    // if we've already visited this interface, but going the other direction,
    // skip it
    if (visited_interfaces.count({pair.second, pair.first}) > 0)
      continue;
    visited_interfaces.insert(pair);

    surface_senses_[next_surface_id] = pair;
    for (const auto &elem : elements) {
      surface_map_[next_surface_id].push_back(elem);
    }
    surfaces().push_back(next_surface_id++);
  }
}

  // now that the boundary faces have been identified, we need to ensure that
  // the normals are consistent for each sideset. The normals of element faces
  // depend on which element is being used to reference the face. This extends
  // to the sideset faces as well, so we need to ensure that the normals are
  // consistent for each sideset. This is done by using the first face for each
  // sideset and treating the first element as the "cannonical" element for the
  // set. This means that all faces in the set should reference elements within
  // the same mesh block to ensure that the orientation of the normals is consistent
  // with respect to that block. Senses in the mesh data structures will be updated
  // accordingly
void LibMeshManager::determine_surface_senses() {
  write_message("Ensuring consistent normals for sideset faces...");
  for (auto &[surface_id, surface_faces] : surface_map_) {
    if (surface_faces.size() == 0) continue;

    // choose the reference block based on the first face in the set
    int reference_block = mesh_id_to_sidepair_.at(surface_faces.at(0)).first()->subdomain_id();
    surface_senses_[surface_id] = {reference_block, ID_NONE};

    for (const auto &face : surface_faces) {
      auto& pair = mesh_id_to_sidepair_.at(face);
      // swap the element positions based on subdomain ID if needed
      if (pair.first()->subdomain_id() != reference_block) {
        pair.swap();
        // if we've swapped a nullptr into the first position, we have a problem
        if (pair.first() == nullptr) fatal_error("Attempting to swap nullptr to first face value");
      }
      // set the sense of the surface with respect to the other block to reverse
      if (pair.second() != nullptr)
        surface_senses_[surface_id] = {reference_block, pair.second()->subdomain_id()};
    }
  }

  // the operation above has likely invlaidated the sidepair to mesh ID map
  // so we need to rebuild it
  sidepair_to_mesh_id_.clear();
  for (const auto& [id, pair] : mesh_id_to_sidepair_) {
    sidepair_to_mesh_id_[pair] = id;
  }
}

void LibMeshManager::create_boundary_sideset() {
  auto& boundary_info = mesh_->get_boundary_info();
  auto boundary_ids = boundary_info.get_boundary_ids();
  int next_boundary_id = boundary_ids.size() == 0 ? 1 : *std::max_element(boundary_ids.begin(), boundary_ids.end()) + 1;

  // put all mesh boundary elements in a special sideset that we can
  // reference later if needed
  // (any faces that are part of the implicit complement in DAGMC parlance)
  for (auto &[id, elem_side] : subdomain_interface_map_) {
    if (id.first == ID_NONE || id.second == ID_NONE) {
      for (const auto &elem : elem_side) {
        auto pair = sidepair(elem);
        boundary_info.add_side(pair.first(), pair.side_num(), next_boundary_id);
      }
    }
  }
  boundary_info.sideset_name(next_boundary_id) = "xdg_boundary";
}

std::vector<MeshID>
LibMeshManager::get_volume_elements(MeshID volume) const {
  std::vector<MeshID> elements;
  libMesh::MeshBase::const_element_iterator it =
      mesh()->active_subdomain_elements_begin(volume);
  libMesh::MeshBase::const_element_iterator it_end =
      mesh()->active_subdomain_elements_end(volume);
  for (; it != it_end; ++it) {
    elements.push_back((*it)->id());
  }
  return elements;
}

int
LibMeshManager::num_volume_elements() const
{
  return num_elements_;
}

std::vector<MeshID>
LibMeshManager::get_surface_faces(MeshID surface) const {
  return surface_map_.at(surface);
}

std::vector<Vertex>
LibMeshManager::element_vertices(MeshID element) const {
  std::vector<Vertex> vertices;
  auto elem = mesh()->elem_ptr(element);
  for (unsigned int i = 0; i < elem->n_nodes(); ++i) {
    auto node = elem->node_ref(i);
    vertices.push_back({node(0), node(1), node(2)});
  }
  return vertices;
}

std::array<Vertex, 3>
LibMeshManager::face_vertices(MeshID element) const {
  const auto& side_pair = sidepair(element);
  std::array<Vertex, 3> vertices;
  for (unsigned int i = 0; i < 3; ++i) {
    vertices[i] = std::move(side_pair.vertex<libMesh::Tet4>(i));
  }
  return vertices;
}

std::vector<MeshID>
LibMeshManager::get_volume_surfaces(MeshID volume) const {
  // walk the surface senses and return the surfaces that have this volume
  // as an entry
  std::vector<MeshID> surfaces;
  for (const auto& [surface, senses] : surface_senses_) {
    if (senses.first == volume || senses.second == volume) {
      surfaces.push_back(surface);
    }
  }
  return surfaces;
}

std::pair<MeshID, MeshID>
LibMeshManager::surface_senses(MeshID surface) const {
  return surface_senses_.at(surface);
}

Sense LibMeshManager::surface_sense(MeshID surface, MeshID volume) const {
  auto senses = surface_senses(surface);
  return volume == senses.first ? Sense::FORWARD : Sense::REVERSE;
}

} // namespace xdg
