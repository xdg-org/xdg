#include <string>

#include "xdg/mfem/mesh_manager.h"
#include "xdg/util/str_utils.h"

namespace xdg {

namespace {
// Key for the interface between two volumes: {lower volume, higher volume}, so
// a face is grouped the same way regardless of which of its elements MFEM
// calls Elem1. Exterior faces are keyed by {volume, ID_NONE} so the real
// volume is always the forward sense.
std::pair<MeshID, MeshID> interface_pair(MeshID vol1, MeshID vol2) {
  if (vol2 == ID_NONE) return {vol1, ID_NONE};
  return {std::min(vol1, vol2), std::max(vol1, vol2)};
}
} // namespace

void MfemMeshManager::load_file(const std::string &filepath) {
  mesh_ = std::make_unique<mfem::Mesh>(filepath.c_str(), 1, 1);
}

void MfemMeshManager::init() {
  // Ensure the mesh is 3-dimensional
  if (mesh_->Dimension() != 3) {
    fatal_error("Mesh must be 3-dimensional");
  }

  // this is done in the mesh reader
  // mesh_->FinalizeTopology();
  
  // set the volumes/attributes...
  // This set should have one entry per volume/attribute type
  for (int i=0; i<mesh_->attributes.Size(); i++) {
    attributes_.insert( mesh_->attributes[i] );
  }

  // Create a set for each volume attribute. Gather the IDs of all the
  // interior elements with this characteristic
  // TODO: This won't work with ParMesh
  //
  // While we are here, we can lazily populate the volume_element_id_map_
  // To do that, we need a vector to store the element IDs into. This
  // is very lazy, and no better than just filling a vector with sequential
  // ints anyway.
  std::vector<MeshID> volume_element_ids(mesh_->GetNE());
  for (int i=0; i<mesh_->GetNE(); i++) {
    int volume_id = mesh_->GetAttribute(i);
    volume_to_element_map_[volume_id].insert(i);
    volume_element_ids[i] = i;
  }

  // Finish the BlockMapping for volume elements
  volume_element_id_map_ = IDBlockMapping<MeshID>(volume_element_ids);

  // We've read in the mesh and counted all the attributes, i.e. a unique
  // list of all the attributes we've seen. Let's copy the contents of
  // attributes_ into volumes_, so the base class has access to the list
  // of volume IDs
  std::copy(attributes_.begin(), attributes_.end(), std::back_inserter(volumes_));

  // search for any implicit surfaces (faces that are the boundary between two
  // attributes/volumes)
  discover_surface_elements();

  // map boundary attributes to discovered interfaces
  map_sidesets_to_discovered_interfaces();

  // create surfaces from boundary attributes and interfaces
  create_surfaces_from_sidesets_and_interfaces();

  determine_surface_senses();

  create_implicit_complement();
}

std::vector<MeshID> MfemMeshManager::get_volume_elements(MeshID volume) const {
  std::vector<MeshID> output;
  
  // Copy the way that libmesh does it, which is if we call
  // this function on the implicit complement, then return an
  // empty vector.
  // Note the deliberate use of attributes_ (which comes from
  // the mfem mesh, naming convention intact) and not volumes_.
  // This is because we would have modified volumes_ to include
  // an implicit complement. More concretetly, attributes_
  // represents the TRUE volumes that the mfem mesh recognises.
  if (attributes_.find(volume) == attributes_.end()) {
    // check that we are looking at the implcit complement
    if (std::find(volumes_.begin(), volumes_.end(), volume) == volumes_.end()) {
      // This is an error now. It's not a true volume
      // or the implicit complement
      std::ostringstream error_message;
      error_message << "Couldn't find volume " << volume << "\n";
      fatal_error(error_message.str());
    }

    // simply return the empty vector if this is the implicit complement
    return output;
  }

  // gather the set from volume_to_element_map_ and copy it into
  // the vector we want to return
  auto volume_set = volume_to_element_map_.at(volume);
  std::copy(volume_set.begin(), volume_set.end(), std::back_inserter(output));
  return output;
}

// Should return all of the sidesets that are a part of this volume
std::vector<MeshID> MfemMeshManager::get_volume_surfaces(MeshID volume) const {
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

std::vector<MeshID> MfemMeshManager::get_surface_faces(MeshID surface) const {
  return surface_map_.at(surface);
}

std::vector<MeshID> MfemMeshManager::face_vertices(MeshID face) const {
  mfem::Array<int> index_array;

  // index_array gets populated with the indices of the vertices itself
  mesh_->GetFaceVertices(face, index_array);

  // copy these indices into std::vector and return it
  std::vector<MeshID> output( index_array.GetData(), index_array.GetData() + index_array.Size() );

  // MFEM orders face vertices so that the normal points out of Elem1. Reverse
  // the winding of faces whose forward volume is on the Elem2 side. Keeping
  // the first vertex in place keeps quads valid (no bow-ties)
  if (face_reversed_[face]) std::reverse(output.begin() + 1, output.end());

  return output;
}

MeshID MfemMeshManager::get_boundary_face_element(MeshID face) const {
  int e1, e2;
  mesh_->GetFaceElements(face, &e1, &e2);
  // the element the face normal points out of
  MeshID element = face_reversed_[face] ? e2 : e1;
  return element >= 0 ? element : ID_NONE;
}

std::pair<int, int> MfemMeshManager::surface_senses(MeshID surface) const {
  return surface_senses_.at(surface);
}

Sense MfemMeshManager::surface_sense(MeshID surface, MeshID volume) const {
  auto senses = surface_senses(surface);
  return volume == senses.first ? Sense::FORWARD : Sense::REVERSE;
}

std::vector<Vertex> MfemMeshManager::element_vertices(MeshID element) const {
  mfem::Array<int> index_array;

  // ask the mesh for the vertices of this element
  mesh_->GetElementVertices(element, index_array);

  std::vector<Vertex> output(index_array.Size());

  for (int i=0; i<index_array.Size(); i++) {
    const double* vertices = mesh_->GetVertex( index_array[i] );

    for (int d=0; d<mesh_->SpaceDimension(); d++) output[i][d] = vertices[d];
  }

  return output;
}

// I've written this extra function because the mesh manager needs to support one
// continuous list of elements for boundary and interior. So at some point we need
// to map them together.
// Update: not sure that's true. moab mesh manager reports the same number of elements
// on the jezebel as mesh->GetNE()
std::vector<Vertex> MfemMeshManager::bdr_element_vertices(MeshID element) const {
  mfem::Array<int> index_array;

  // ask the mesh for the vertices of this element
  mesh_->GetBdrElementVertices(element, index_array);

  std::vector<Vertex> output(index_array.Size());

  for (int i=0; i<index_array.Size(); i++) {
    const double* vertices = mesh_->GetVertex( index_array[i] );

    for (int d=0; d<mesh_->SpaceDimension(); d++) output[i][d] = vertices[d];
  }

  return output;
}

MeshID MfemMeshManager::adjacent_element(MeshID element, int face) const {
  // Would be nice if we had the face element accessor still available
  // TODO: do something novel if we are already on the boundary
  mfem::Array<int> faces, ori; // don't care about ori

  mesh_->GetElementFaces(element, faces, ori);

  if (face==ID_NONE) return ID_NONE;

  // not quite. faces[face] is just the faceID. We need
  // the element that it's connected to.
  int e1, e2;
  mesh_->GetFaceElements(faces[face], &e1, &e2);

  // e1 and e2 are now the element IDs of two elements
  // that share this face.
  assert(element == e1 or element == e2);

  if      (element == e1) return e2;
  else if (element == e2) return e1;
  
  fatal_error("Shouldn't reach this far!");
}

std::pair<MeshID, MeshID> MfemMeshManager::face_volumes(int face) const {
  // TODO: This won't work with ParMesh, where Elem2No < 0 can also mean the
  // neighbour lives on another rank
  int e1, e2;
  mesh_->GetFaceElements(face, &e1, &e2);
  return {mesh_->GetAttribute(e1), e2 >= 0 ? mesh_->GetAttribute(e2) : ID_NONE};
}

void MfemMeshManager::discover_surface_elements() {
  // MFEM numbers every face of the mesh, so we can walk them directly and keep
  // any face whose elements belong to different volumes.
  interface_faces_.clear();
  for (int face = 0; face < mesh_->GetNumFaces(); face++) {
    auto [vol1, vol2] = face_volumes(face);
    if (vol1 == vol2) continue;
    InterfacePair pair = interface_pair(vol1, vol2);
    interface_faces_[pair].push_back(face);
  }
}

void MfemMeshManager::map_sidesets_to_discovered_interfaces() {
  // Every boundary element is a face of the mesh, so it is always represented
  // in the discovered interfaces unless it is internal to a single volume, in
  // which case it doesn't separate anything and is ignored (as it is for
  // libMesh).
  sideset_interface_faces_.clear();
  std::unordered_map<int, int> face_to_sideset;
  for (int bdr_element = 0; bdr_element < mesh_->GetNBE(); bdr_element++) {
    const int sideset = mesh_->GetBdrAttribute(bdr_element);
    const int face = mesh_->GetBdrElementFaceIndex(bdr_element);

    auto [vol1, vol2] = face_volumes(face);
    if (vol1 == vol2) continue;

    // a face can only belong to one surface
    auto [it, inserted] = face_to_sideset.emplace(face, sideset);
    if (!inserted) {
      if (it->second != sideset) {
        fatal_error("Face {} belongs to boundary attributes {} and {}, which the MFEM "
                    "mesh manager does not support", face, it->second, sideset);
      }
      continue; // duplicate boundary element on the same face
    }

    InterfacePair pair = interface_pair(vol1, vol2);
    sideset_interface_faces_[sideset][pair].push_back(face);
  }
}

void MfemMeshManager::create_surfaces_from_sidesets_and_interfaces() {
  // Mirrors LibMeshManager::create_surfaces_from_sidesets_and_interfaces:
  // 1. each boundary attribute becomes one surface per interface it lies on.
  //    If it only lies on one interface the boundary attribute ID is reused as
  //    the surface ID, otherwise new surface IDs are assigned.
  // 2. faces of each interface that no boundary attribute covers become a new
  //    surface with a new surface ID.
  surfaces_.clear();
  surface_map_.clear();
  surface_senses_.clear();
  sideset_surface_map_.clear();

  // boundary attribute IDs are reserved so implicit surfaces never take them
  std::set<MeshID> used_surface_ids;
  for (int i = 0; i < mesh_->bdr_attributes.Size(); i++) {
    used_surface_ids.insert(mesh_->bdr_attributes[i]);
  }

  MeshID next_surface_id = 1;
  auto next_available_surface_id = [&]() {
    while (used_surface_ids.count(next_surface_id) != 0) next_surface_id++;
    used_surface_ids.insert(next_surface_id);
    return next_surface_id++;
  };

  auto add_surface = [&](MeshID surface_id, const InterfacePair& pair, const std::vector<int>& faces) {
    surfaces_.push_back(surface_id);
    surface_senses_[surface_id] = pair;
    surface_map_[surface_id] = std::vector<MeshID>(faces.begin(), faces.end());
  };

  std::vector<bool> covered_faces(mesh_->GetNumFaces(), false);

  // std::map keeps boundary attributes and interfaces sorted, so surface IDs
  // are assigned deterministically
  for (const auto& [sideset_id, interface_groups] : sideset_interface_faces_) {
    const bool reuse_sideset_id = interface_groups.size() == 1;
    for (const auto& [pair, faces] : interface_groups) {
      MeshID surface_id = reuse_sideset_id ? sideset_id : next_available_surface_id();
      add_surface(surface_id, pair, faces);
      for (int face : faces) covered_faces[face] = true;
      sideset_surface_map_[sideset_id].push_back(surface_id);
    }
  }

  for (const auto& [pair, faces] : interface_faces_) {
    std::vector<int> remaining_faces;
    std::copy_if(faces.begin(), faces.end(), std::back_inserter(remaining_faces),
                 [&](int face) { return !covered_faces[face]; });
    if (remaining_faces.empty()) continue;

    add_surface(next_available_surface_id(), pair, remaining_faces);
  }
}

void MfemMeshManager::determine_surface_senses() {
  // MFEM orders the vertices of a face so that its normal points out of
  // Elem1, but Elem1 may be on either side of a surface. Mark the faces whose
  // Elem1 is not in the forward volume so face_vertices can flip them, giving
  // every face of a surface a normal that points out of its forward volume.
  face_reversed_.assign(mesh_->GetNumFaces(), false);
  for (const auto& [surface_id, faces] : surface_map_) {
    const MeshID forward_volume = surface_senses_.at(surface_id).first;
    for (MeshID face : faces) {
      face_reversed_[face] = face_volumes(face).first != forward_volume;
    }
  }
}

MeshID MfemMeshManager::create_volume() {
  MeshID next_volume_id = *std::max_element(volumes_.begin(), volumes_.end()) + 1;
  return next_volume_id;
}

void MfemMeshManager::add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite) {
  auto senses = surface_senses(surface);
  if (sense == Sense::FORWARD) {
    if (!overwrite && senses.first != ID_NONE) {
      fatal_error("Surface already has a forward sense");
    }
    surface_senses_[surface] = {volume, senses.second};
  }

  else {
    if (!overwrite && senses.second != ID_NONE) {
      fatal_error("Surface already has a reverse sense");
    }
      surface_senses_[surface] = {senses.first, volume};
  }
}

void MfemMeshManager::parse_metadata() {
  auto& attr_sets = mesh_->attribute_sets;

  for (auto vol_name : attr_sets.GetAttributeSetNames()) {
    const mfem::Array<int> &attrs = attr_sets.GetAttributeSet(vol_name);
    remove_substring(vol_name, "mat:");

    // loop over every volume that is named the same thing
    for (int i = 0; i < attrs.Size(); i++) {
      const int vol_id = attrs[i];
      if (vol_name.empty())
        volume_metadata_[{vol_id, PropertyType::MATERIAL}] = VOID_MATERIAL;
      else
        volume_metadata_[{vol_id, PropertyType::MATERIAL}] = {PropertyType::MATERIAL, vol_name};

    }
  }

  // ditto for boundaries
  auto& bdr_attr_sets = mesh_->bdr_attribute_sets;
  for (auto bdr_name : bdr_attr_sets.GetAttributeSetNames()) {
    const mfem::Array<int> &attrs = bdr_attr_sets.GetAttributeSet(bdr_name);
    remove_substring(bdr_name, "boundary:");

    // loop over every boundary attribute that is named the same thing, and
    // every surface created from it
    for (int i = 0; i < attrs.Size(); i++) {
      auto it = sideset_surface_map_.find(attrs[i]);
      if (it == sideset_surface_map_.end()) continue;

      for (auto surface : it->second) {
        const auto key = std::make_pair(surface, PropertyType::BOUNDARY_CONDITION);
        if (surface_metadata_.count(key) != 0 &&
            surface_metadata_.at(key).value != bdr_name) {
          fatal_error("Conflicting boundary metadata assigned to surface {}", surface);
        }

        surface_metadata_[key] = {PropertyType::BOUNDARY_CONDITION, bdr_name};
      }
    }
  }
}

SurfaceFaceType MfemMeshManager::get_surface_face_type(MeshID surface) const {
  auto faces = this->get_surface_faces(surface);
  if (faces.empty()) {
    fatal_error("Surface {} has no faces; cannot determine face type", surface);
  }
  return face_type(faces.front());
}

SurfaceFaceType MfemMeshManager::face_type(MeshID face) const {
  mfem::Geometry::Type geom = mesh_->GetFaceGeometry(face);

  switch (geom) {
    case mfem::Geometry::TRIANGLE: return SurfaceFaceType::TRI;
    case mfem::Geometry::SQUARE:   return SurfaceFaceType::QUAD;
    default:
      fatal_error("Unsupported MFEM face geometry for face {}", face);
  }
  return SurfaceFaceType::UNSUPPORTED;
}

VolumeElementType MfemMeshManager::get_volume_element_type(MeshID volume) const {
  auto elements = this->get_volume_elements(volume);
  if (elements.empty()) {
    fatal_error("Volume {} has no elements; cannot determine element type", volume);
  }
  return element_type(elements.front());
}

VolumeElementType MfemMeshManager::element_type(MeshID element) const {
  mfem::Geometry::Type geom = mesh_->GetElementBaseGeometry(element);

  switch (geom) {
    case mfem::Geometry::TETRAHEDRON: return VolumeElementType::TET;
    case mfem::Geometry::CUBE:        return VolumeElementType::HEX;
    default:
      fatal_error("Unsupported MFEM element geometry for element {}", element);
  }
  return VolumeElementType::UNSUPPORTED;
}


// helper function to convert mfem's element types to xdg
VolumeElementType GetTypeFromMfem( mfem::Element::Type t ) {
  switch (t) {
    case mfem::Element::TETRAHEDRON: return VolumeElementType::TET;
    case mfem::Element::HEXAHEDRON:  return VolumeElementType::HEX;
    default:
      fatal_error("Unsupported element type\n");
  }
}

} // namespace xdg
