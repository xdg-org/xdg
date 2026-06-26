#include <string>

#include "xdg/mfem/mesh_manager.h"

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
  for (int i=0; i<mesh_->GetNE(); i++) {
    int volume_id = mesh_->GetAttribute(i);
    volume_to_element_map_[volume_id].insert(i);
  }

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
  }

  // We've read in the mesh and counted all the attributes, i.e. a unique
  // list of all the attributes we've seen. Let's copy the contents of
  // attributes_ into volumes_, so the base class has access to the list
  // of volume IDs
  std::copy(attributes_.begin(), attributes_.end(), std::back_inserter(volumes_));

  // set these two attributes related to interior/boundary faces
  num_interior_faces_ = mesh_->GetNumFaces();
  num_boundary_faces_ = mesh_->GetNBE();
}

// TODO: very slow, and could be done during init()
std::vector<MeshID> MfemMeshManager::get_volume_elements(MeshID volume) const {
  if (attributes_.find(volume) == attributes_.end()) {
    std::ostringstream output;
    output << "Couldn't find volume " << volume << "\n";
    fatal_error(output.str());
  }

  std::vector<MeshID> output;

  // gather all the element IDs that have this attribute
  // this method is absolutely criminal. Could be done at the start
  // when we run over all the elements anyway...
  for (int i=0; i<mesh_->GetNE(); i++) {
    if ( mesh_->GetAttribute(i) == volume ) output.push_back(i);
  }

  return output;
}

SurfaceElementType MfemMeshManager::get_surface_element_type(MeshID element) const {
  auto mfem_element_type = mesh_->GetBdrElement(element)->GetType();
  return GetSurfaceElementTypeFromMfem(mfem_element_type);
}

// Should return all of the sidesets that are a part of this volume
std::vector<MeshID> MfemMeshManager::get_volume_surfaces(MeshID volume) const {
  // get the set associated with this volume
  const std::set<int>& sidesets = volumes_to_sidesets_.at(volume);

  // create a vector from this set
  std::vector<int> output(sidesets.begin(), sidesets.end());

  return output;
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

std::array<Vertex, 3> MfemMeshManager::face_vertices(MeshID element) const {
  std::array<Vertex, 3> output;
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
    // create an mfem array to be passed into Mesh::GetFaceVertices.
    // this gets populated with the indices of the vertices itself
    mesh_->GetFaceVertices(element, index_array);
  }

  for (int i=0; i<index_array.Size(); i++) {
    const double* vertices = mesh_->GetVertex( index_array[i] );

    for (int d=0; d<mesh_->SpaceDimension(); d++) output[i][d] = vertices[d];
  }

  return output;
}

std::pair<int, int> MfemMeshManager::surface_senses(MeshID surface) const {
  // I am trying to get the raytracer preparation routines working with
  // the jezebel, so just return {-1, 1}. i.e. implicit_complement, interior_volume.
  // Even though we haven't created implicit_complement yet.
  warning("MfemMeshManager::surface_senses() is hardcoded for single-volume meshes");

  // TODO: make the second value one more than the largest volume ID we've seen
  // i.e. since the only volume in the jezebel/brick is 1, the second id must be 2,
  // to denote the implicit complement
  return {1,2};
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

  // face is in range [0,3). So we just need the one
  // that the caller asked for
  return faces[face];
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

// this second function is somewhat redundant. The mfem enum captures all
// of the possible geometries, in all possible dimensions...
SurfaceElementType GetSurfaceElementTypeFromMfem( mfem::Element::Type t ) {
  switch (t) {
    case mfem::Element::TRIANGLE:      return SurfaceElementType::TRI;
    case mfem::Element::QUADRILATERAL: return SurfaceElementType::QUAD;
    default:
      fatal_error("Unsupported element type\n");
  }
}


} // namespace xdg