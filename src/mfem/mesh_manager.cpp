#include <string>

#include "xdg/mfem/mesh_manager.h"
#include "xdg/util/str_utils.h"

namespace xdg {
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

  // create a set for capturing all of the sideset IDs
  // without repeats
  std::set<int> sideset_ids;

  // same for boundary attributes
  for (int i=0; i<mesh_->GetNBE(); i++) {
    int sideset = mesh_->GetBdrAttribute(i);

    sideset_to_element_map_[sideset].insert(i);

    // We also want to count the number of sidesets that each volume has.
    // So, while we are looping over each boundary element, we look at
    // which sideset it's a member of. We then look at
    // its immediate neighbour on the interior of the mesh. We query
    // this neighbour for which volume it's a member of, and register
    // this sideset as a member of that volume.
    // We want each sideset to be a member of exactly one volume, but
    // that's probably too much to ask.
    int elem_no, info;
    mesh_->GetBdrElementAdjacentElement(i, elem_no, info);

    int volume = mesh_->GetAttribute(elem_no);
    volumes_to_sidesets_[volume].insert(sideset);

    // we want to populate the surfaces_ array from the base class
    sideset_ids.insert(sideset);
  }
  
  /*
  // we wanna check how many implicit boundaries we detect. So let's
  // create a set to collect them
  std::set< std::pair<int,int> > implicit_bdr;
  for (int f=0; f<mesh_->GetNumFaces(); f++) {
    int e1, e2;
    mesh_->GetFaceElements(f, &e1, &e2);

    // if the el index is -1, then just set the volume id to -1.
    // this means the outside
    int vol1 = (e1==-1) ? -1 : mesh_->GetAttribute(e1);
    int vol2 = (e2==-1) ? -1 : mesh_->GetAttribute(e2);

    if (vol1!=vol2) {
      // we have found implicit boundary. add to list!
      if (vol1<vol2) implicit_bdr.insert( {vol1, vol2} );
      else           implicit_bdr.insert( {vol2, vol1} );
    }

  }

  // let's see what we got
  std::cout << "\n\nPrinting the boundaries we discovered!!!\n";
  auto iter = implicit_bdr.begin();
  for (; iter!=implicit_bdr.end(); iter++) {
    std::cout << "detected bdr between vol " << iter->first << " and " << iter->second << "\n";
  }
  */

  // We've read in the mesh and counted all the attributes, i.e. a unique
  // list of all the attributes we've seen. Let's copy the contents of
  // attributes_ into volumes_, so the base class has access to the list
  // of volume IDs
  std::copy(attributes_.begin(), attributes_.end(), std::back_inserter(volumes_));

  // ditto for surfaces
  std::copy(sideset_ids.begin(), sideset_ids.end(), std::back_inserter(surfaces_));

  // set these two attributes related to interior/boundary faces
  num_interior_faces_ = mesh_->GetNumFaces();
  num_boundary_faces_ = mesh_->GetNBE();

  determine_surface_senses();

  create_implicit_complement();
}

// TODO: very slow, and could be done during init()
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
      std::ostringstream output;
      output << "Couldn't find volume " << volume << "\n";
      fatal_error(output.str());
    }

    // simply return the empty vector if this is the implicit
    // complement
    return output;
  }

  // gather all the element IDs that have this attribute
  // this method is absolutely criminal. Could be done at the start
  // when we run over all the elements anyway...
  for (int i=0; i<mesh_->GetNE(); i++) {
    if ( mesh_->GetAttribute(i) == volume ) output.push_back(i);
  }

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
  // get the set associated with this surface
  const std::set<int>& boundary_faces = sideset_to_element_map_.at(surface);

  // copy it into a vector
  std::vector<int> output(boundary_faces.begin(), boundary_faces.end());

  // We want [0->mesh_->GetNumFaces() ) to represent interior faces.
  // and we want [ mesh_->GetNumFaces(), mesh_->GetNumFaces() + mesh_->GetNBE() )
  // to represent the boundary faces.
  // When we query the face vertices later, we need to take this
  // mapping into account. All we do here is increase the MeshIDs by
  // num_interior_faces_ to effect this mapping
  std::transform( output.begin(), output.end(), output.begin(), [&](int in){ return in + num_interior_faces_; } );

  return output;
}

std::vector<MeshID> MfemMeshManager::face_vertices(MeshID element) const {
  mfem::Array<int> index_array;
  
  if (element >= num_interior_faces_) {
    // we are actually talking about a boundary element here. this is
    // us taking the mapping into account. see comments at the end of
    // get_surfaces_faces
    MeshID bdr_element = element - num_interior_faces_;
    mesh_->GetBdrElementVertices(bdr_element, index_array);

    mfem::Element* bdr_el = mesh_->GetBdrElement(bdr_element);
    int* vertices = bdr_el->GetVertices();
  }

  else {
    // index_array gets populated with the indices of the vertices itself
    mesh_->GetFaceVertices(element, index_array);
  }

  // for (int i=0; i<index_array.Size(); i++) {
  //   const double* vertices = mesh_->GetVertex( index_array[i] );

  //   for (int d=0; d<mesh_->SpaceDimension(); d++) output[i][d] = vertices[d];
  // }

  // copy these indices into std::vector and return it
  std::vector<MeshID> output( index_array.GetData(), index_array.GetData() + index_array.Size() );
  return output;
}

std::pair<int, int> MfemMeshManager::surface_senses(MeshID surface) const {

  // TODO: make the second value one more than the largest volume ID we've seen
  // i.e. since the only volume in the jezebel/brick is 1, the second id must be 2,
  // to denote the implicit complement
  return surface_senses_.at(surface);
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

void MfemMeshManager::determine_surface_senses() {
  for (auto &[surface_id, surface_faces] : sideset_to_element_map_) {
    if (surface_faces.size() == 0) continue;

    int face_index = *surface_faces.begin();

    // first, get the volume that this sideset is living on.
    // we do it the dumb way
    int elem_no, info;
    mesh_->GetBdrElementAdjacentElement(face_index, elem_no, info);

    // TODO: does this return the same number for every element on
    // sideset 3?

    int volume = mesh_->GetAttribute(elem_no);

    int face_no = mesh_->GetBdrElementFaceIndex(face_index);
    // check if the connectivity is still there
    int e1, e2;
    mesh_->GetFaceElements(face_no, &e1, &e2);

    // if we have both elements nontrivial (i.e. != -1) then we ask the
    // the mesh which is elem1 and which is elem2
    // The normal vector is supposed to point from the reverse sense
    // to the forward sense
    if (e1 != -1 and e2 !=-1) {
      auto face_el_tx = mesh_->GetFaceElementTransformations(face_no);

      // check that face_el_tx has picked out the correct elements
      assert( (face_el_tx->Elem1No == e1 or face_el_tx->Elem1No == e2)
        and   (face_el_tx->Elem2No == e1 or face_el_tx->Elem2No == e2)
      );

      // Elem1 is the reverse sense and Elem2 is the forwards sense, since
      // by construction, this is the way the normal vectors are pointing.
      // We can't put the element IDs in to the array, so we have to ask
      // the mesh for their attributes
      surface_senses_[surface_id] = {
        mesh_->GetAttribute(face_el_tx->Elem1No), mesh_->GetAttribute(face_el_tx->Elem2No)
      };
    }

    // We have a surface on a true boundary. The second element is simply
    // the implcit complement
    else {
      surface_senses_[surface_id] = {volume, ID_NONE};
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

    // loop over every surface that is named the same thing
    for (int i = 0; i < attrs.Size(); i++) {
      const int bdr_id = attrs[i];
      const auto key = std::make_pair(bdr_id, PropertyType::BOUNDARY_CONDITION);

      surface_metadata_[key] = {PropertyType::BOUNDARY_CONDITION, bdr_name};
    }
  }
}

// This is not quite correct. mfem does support mixed meshes.
// The intention of the caller is that the argument (surface) corresponds
// to the sideset. So we need to find a typical element from the sideset
// that is marked by the argument surface. For now, this will do
SurfaceFaceType MfemMeshManager::get_surface_face_type(MeshID surface) const {
  mfem::Geometry::Type geom = mesh_->GetFaceGeometry(surface);

  switch(geom) {
    case mfem::Geometry::TRIANGLE:     return SurfaceFaceType::TRI;
    case mfem::Geometry::TETRAHEDRON : return SurfaceFaceType::QUAD;
    default:
      fatal_error("Unsupported geom");
  }
}

// Same problem as above. Volume is supposed to be a block_id, and this
// function is interpreting it as an element index. We should have a LOT more
// elements than blocks, so it's safe, but wrong. When dealing with a mixed
// mesh, it will fail tests
VolumeElementType MfemMeshManager::get_volume_element_type(MeshID volume) const {
  mfem::Geometry::Type geom = mesh_->GetElementBaseGeometry(volume);

  switch (geom) {
    case mfem::Geometry::TETRAHEDRON: return VolumeElementType::TET;
    case mfem::Geometry::TRIANGLE:    return VolumeElementType::HEX;
    default: 
      fatal_error("Unsupported geom");
  }

  return VolumeElementType::TET;
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
