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
LibMeshManager::LibMeshManager(const libMesh::MeshBase* ptr) {
  mesh_ = ptr;
}

LibMeshManager::LibMeshManager() : MeshManager() {}

void LibMeshManager::load_file(const std::string &filepath) {
  managed_mesh_ = std::make_unique<libMesh::Mesh>(*XDGConfig::config().libmesh_comm(), 3);
  managed_mesh_->read(filepath);
  mesh_ = managed_mesh_.get();
}

void LibMeshManager::init() {
  // ensure that the mesh is 3-dimensional, for our use case this is expected
  if (mesh()->mesh_dimension() != 3) {
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

  // map sidesets to discovered interfaces where possible
  map_sidesets_to_discovered_interfaces();

  // create surfaces from sidesets and interfaces
  create_surfaces_from_sidesets_and_interfaces();

  // determine the senses of the surfaces, defines
  // the mesh-based topology of the geometry
  determine_surface_senses();

  // create an implicit complement
  create_implicit_complement();

  // libMesh initialization
  if (managed_mesh_) {
    managed_mesh_->prepare_for_use();
  }

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

xdg::Vertex
LibMeshManager::vertex_coordinates(MeshID vertex_id) const {
  const auto node_ptr = mesh()->node_ptr(vertex_id);
  if (!node_ptr) {
    fatal_error("Invalid vertex ID in vertex_coordinates");
  }
  return {(*node_ptr)(0), (*node_ptr)(1), (*node_ptr)(2)};
}

std::vector<MeshID>
LibMeshManager::element_connectivity(MeshID element) const {
  const auto elem_ptr = mesh()->elem_ptr(element);
  if (!elem_ptr) {
    fatal_error("Invalid element ID in connectivity");
  }
  std::vector<MeshID> conn;
  for (unsigned int i = 0; i < elem_ptr->n_nodes(); i++) {
    conn.push_back(elem_ptr->node_ptr(i)->id());
  }
  return conn;
}

std::vector<MeshID>
LibMeshManager::face_connectivity(MeshID face) const {
  const auto& sp = sidepair(face);
  auto face_ptr = sp.face_ptr();
  if (!face_ptr) {
    fatal_error("Invalid face in face_connectivity");
  }

  std::vector<MeshID> conn;
  conn.reserve(face_ptr->n_nodes());
  for (unsigned int i = 0; i < face_ptr->n_nodes(); ++i) {
    conn.push_back(face_ptr->node_id(i));
  }
  return conn;
}

MeshID
LibMeshManager::get_boundary_face_element(MeshID face) const {
  const auto& sp = sidepair(face);
  if (sp.first()) {
    return sp.first()->id();
  }
  return ID_NONE;
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
  for (const auto& [sideset_id, surfaces] : sideset_surface_map_) {
    if (sideset_name_map.find(sideset_id) == sideset_name_map.end()) {
      continue;
    }

    std::string sideset_name = sideset_name_map[sideset_id];
    remove_substring(sideset_name, "boundary:");

    for (auto surface : surfaces) {
      const auto key = std::make_pair(surface, PropertyType::BOUNDARY_CONDITION);
      if (surface_metadata_.count(key) != 0 &&
          surface_metadata_.at(key).value != sideset_name) {
        fatal_error("Conflicting boundary metadata assigned to surface {}", surface);
      }

      surface_metadata_[key] = {PropertyType::BOUNDARY_CONDITION, sideset_name};
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

void LibMeshManager::map_id_spaces() {
  // build the BlockMapping for volume elements
  std::vector<MeshID> volume_element_ids;
  volume_element_ids.reserve(mesh()->n_active_elem());
  for (const auto *elem : mesh()->active_element_ptr_range()) {
    volume_element_ids.push_back(elem->id());
  }
  volume_element_id_map_ = IDBlockMapping<MeshID>(volume_element_ids);

  // build the BlockMapping for vertex IDs
  std::vector<MeshID> vertex_ids;
  vertex_ids.reserve(mesh()->n_nodes());
  for (const auto *node : mesh()->node_ptr_range()) {
    vertex_ids.push_back(node->id());
  }
  vertex_id_map_ = IDBlockMapping<MeshID>(vertex_ids);
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
  volume_element_id_map_ = IDBlockMapping<MeshID>(volume_element_ids);
}

void LibMeshManager::map_sidesets_to_discovered_interfaces() {
  // validate that each sideset face is already represented by one of the
  // discovered interface sets. Same-block internal faces may appear in a
  // sideset but are ignored here.
  using InterfacePair = std::pair<MeshID, MeshID>;
  auto resolve_interface_pair = [this](const InterfacePair& pair, InterfacePair& resolved_pair) {
    if (subdomain_interface_map_.count(pair) != 0) {
      resolved_pair = pair;
      return true;
    }

    InterfacePair reversed_pair {pair.second, pair.first};
    if (subdomain_interface_map_.count(reversed_pair) != 0) {
      resolved_pair = reversed_pair;
      return true;
    }

    return false;
  };

  sideset_interface_map_.clear();
  sideset_interface_face_map_.clear();
  // loop over discovered interface sets of faces and validate that each face in
  // the explicit sidesets is represented in one of the discovered interface
  // sets. As we go, we will also build a mapping of the explicit sidesets to
  // the discovered interfaces that they overlap with and the faces they
  // contribute to each interface for use in surface creation and metadata
  // assignment in subsequent steps
  for (const auto& [sideset_id, sideset_elems] : sideset_face_map_) {
    if (sideset_elems.empty()) continue;

    size_t missing_face_count = 0;
    for (const auto& face_id : sideset_elems) {
      const auto& face = sidepair(face_id);
      if (face.second() != nullptr &&
          face.first()->subdomain_id() == face.second()->subdomain_id()) {
        continue;
      }

      // build interface pair for this face using the subdomain IDs of the elements on either side.
      // If there is no neighbor element, use ID_NONE for the second subdomain ID.
      // Then resolve this pair against the discovered interface pairs to determine which interface set this face belongs to.
      InterfacePair face_pair {
          face.first()->subdomain_id(),
          face.second() ? face.second()->subdomain_id() : ID_NONE};

      // If for some reason the face is internal to a subdomain, continue and do
      // not associate it with an interface pair. This may occur in cases where
      // the mesh has internal faces that are marked as sidesets, which is not
      // ideal but we will allow it to pass through without error for now since
      // it does not impact the mesh-based topology of the geometry.
      if (face_pair.first == face_pair.second) {
        continue;
      }

      InterfacePair resolved_pair;
      // If the interface pair cannot be located in a disocvered interface set,
      // then this face is not properly represented by the mesh-based topology and the missing face count is incremented.
      if (!resolve_interface_pair(face_pair, resolved_pair)) {
        missing_face_count++;
        continue;
      }

      if (subdomain_interface_map_.at(resolved_pair).count(face_id) == 0) {
        missing_face_count++;
        continue;
      }

      // If this face is properly represented by a discovered interface, then we
      // will record the association between the sideset and the interface pair
      // it belongs to, as well as the face's membership in that interface pair
      // for use in surface creation and metadata assignment in subsequent steps
      sideset_interface_map_[sideset_id].insert(resolved_pair);
      sideset_interface_face_map_[sideset_id][resolved_pair].push_back(face_id);
    }

    // If any of the faces in explicit sidesets are not represented by the
    // discovered mesh-based topology, the number of missing faces is reported
    // and an error is raised.
    if (missing_face_count > 0) {
      fatal_error(
          "Sideset {} has {} face(s) that are not represented by discovered interface sets",
          sideset_id,
          missing_face_count);
    }
  }
}

void LibMeshManager::create_surfaces_from_sidesets_and_interfaces() {
  // method overview:
  // 1. loop over explicit sidesets and create surfaces for each associated
  //    interface pair, tracking a mapping from the sideset to the surface(s) as
  //    we go to support a one-to-many mapping of metadata associated with the
  //    sideset to be registered fully when parse_metadata is called. If an
  //    explicit sideset is only associated with one interface pair, we will
  //    reuse the sideset ID for the surface since there is no risk of ID
  //    conflicts in this case, otherwise we will assign a new surface ID.
  // 2. loop over discovered interface pairs and create surfaces for any pairs
  //    that are not fully covered by explicit sidesets, assigning any remaining
  //    metadata from associated sidesets to the surface  as we go. Since these
  //    surfaces are not associated with explicit sidesets, we will assign new
  //    surface IDs to all of these surfaces to avoid any potential ID
  //    conflicts.
  using InterfacePair = std::pair<MeshID, MeshID>;
  surfaces().clear();
  surface_map_.clear();
  surface_senses_.clear();
  sideset_surface_map_.clear();

  // to ensure that we do not reuse surface IDs, we will keep track of the
  // surface IDs that have already been assigned to explicit sidesets and the
  // interface pairs they are associated with
  std::set<MeshID> reserved_surface_ids;
  for (const auto& [sideset_id, _] : sideset_face_map_) {
    reserved_surface_ids.insert(sideset_id);
  }

  // we will also keep track of the surface IDs that have been assigned to
  // surfaces created for explicit sidesets and the interface pairs they are
  // associated with to ensure that we do not reuse these IDs when creating
  // surfaces for the remaining implicit interfaces that are not covered by
  // explicit sidesets
  std::set<MeshID> used_surface_ids;
  MeshID next_surface_id = 1;
  auto next_available_surface_id = [&]() {
    while (reserved_surface_ids.count(next_surface_id) != 0 ||
           used_surface_ids.count(next_surface_id) != 0) {
      next_surface_id++;
    }
    used_surface_ids.insert(next_surface_id);
    return next_surface_id++;
  };

  std::unordered_map<InterfacePair, std::set<MeshID>, MeshIDPairHash> covered_interface_faces;

  // loop over explicit sidesets and create surfaces for each associated
  // interface pair, assigning metadata from the sideset to the surface as we
  // go. If an explicit sideset is only associated with one interface pair, we
  // will reuse the sideset ID for the surface since there is no risk of ID
  // conflicts in this case, otherwise we will assign a new surface ID.
  std::vector<MeshID> sorted_sideset_ids;
  sorted_sideset_ids.reserve(sideset_interface_face_map_.size());
  for (const auto& [sideset_id, _] : sideset_interface_face_map_) {
    sorted_sideset_ids.push_back(sideset_id);
}
  std::sort(sorted_sideset_ids.begin(), sorted_sideset_ids.end());

  // here we loop over all explicit sidesets in sorted order to ensure that we
  // assign surface IDs in a deterministic way
  for (auto sideset_id : sorted_sideset_ids) {
    const auto& interface_face_groups = sideset_interface_face_map_.at(sideset_id);
    bool reuse_sideset_id = interface_face_groups.size() == 1;
    // for each interface pair associated with this explicit sideset, we will
    // create a surface and assign metadata from the sideset to the surface as
    // we go. This is where explicit sidesets may be split into multiple
    // surfaces if they are associated with multiple interface pairs, so we need
    // to loop over each interface pair associated with the sideset to create
    // surfaces for each pair
    for (const auto& [pair, faces] : interface_face_groups) {
      MeshID surface_id = reuse_sideset_id ? sideset_id : next_available_surface_id();
      if (reuse_sideset_id) {
        used_surface_ids.insert(surface_id);
      }

      surface_senses_[surface_id] = pair;
      for (const auto& face : faces) {
        surface_map_[surface_id].push_back(face);
        covered_interface_faces[pair].insert(face);
      }
      surfaces().push_back(surface_id);
      // update mapping of explicit sidesets to surfaces. This is important for
      // propagating metadata on explicit sidesets to surfaces in the case that
      // a single sideset spans multiple subdomain interfaces.
      sideset_surface_map_[sideset_id].push_back(surface_id);
    }
  }

  // now that explicit sidesets have been processed, we loop over remaining
  // discovered subdomain interfaces to complete the topological definition of
  // the mesh
  std::vector<InterfacePair> sorted_interface_pairs;
  sorted_interface_pairs.reserve(subdomain_interface_map_.size());
  for (const auto& [pair, _] : subdomain_interface_map_) {
    sorted_interface_pairs.push_back(pair);
  }
  std::sort(sorted_interface_pairs.begin(), sorted_interface_pairs.end());

  for (const auto& pair : sorted_interface_pairs) {
    std::vector<MeshID> remaining_faces;
    const auto& elements = subdomain_interface_map_.at(pair);
    remaining_faces.reserve(elements.size());
    for (const auto& face : elements) {
      if (covered_interface_faces[pair].count(face) == 0) {
        remaining_faces.push_back(face);
      }
    }

    if (remaining_faces.empty()) {
      continue;
    }

    MeshID surface_id = next_available_surface_id();
    surface_senses_[surface_id] = pair;
    for (const auto& face : remaining_faces) {
      surface_map_[surface_id].push_back(face);
    }
    surfaces().push_back(surface_id);
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

int LibMeshManager::num_vertices() const {
  return mesh()->n_nodes();
}

std::vector<MeshID>
LibMeshManager::get_surface_faces(MeshID surface) const {
  return surface_map_.at(surface);
}

std::vector<Vertex>
LibMeshManager::element_vertices(MeshID element) const {
  const auto& elem = mesh()->elem_ref(element);
  std::vector<Vertex> vertices(elem.n_nodes());
  for (unsigned int i = 0; i < elem.n_nodes(); ++i) {
    const auto& node = elem.node_ref(i);
    vertices[i] = {node(0), node(1), node(2)};
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
