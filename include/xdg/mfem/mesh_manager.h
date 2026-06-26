#ifndef _XDG_MFEM_MESH_MANAGER
#define _XDG_MFEM_MESH_MANAGER

#include <memory>

#include "xdg/constants.h"
#include "xdg/element_face_accessor.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/error.h"

#include "mfem/mfem.hpp"

namespace xdg {
class MfemMeshManager : public MeshManager {
public:

  MfemMeshManager() {};

  ~MfemMeshManager() override = default;

  // Backend methods

  void load_file(const std::string &filepath) override;

  void init() override;

  // Accessors
  const mfem::Mesh* mesh() const { return mesh_.get(); }
  mfem::Mesh* mesh() { return mesh_.get(); }

  // Interface methods
  MeshLibrary mesh_library() const override { return MeshLibrary::MFEM; }

  // This info might not be available in mfem
  int num_volumes() const override {
    return mesh_->attribute_sets.GetAttributeSetNames().size();
  }

  int num_surfaces() const override {
    return mesh_->bdr_attribute_sets.GetAttributeSetNames().size();
  }

  int num_ents_of_dimension(int dim) const override {
    switch (dim) {
      case 3: return num_volumes();
      case 2: return num_surfaces();
      default: return 0;
    }
  }

  // I think we need to count the number of elements with the attribute "volume"
  int num_volume_elements(MeshID volume) const override {
    fatal_error("MfemMeshManager::num_volume_elements() not implemented yet");
  }

  int num_volume_elements() const override {
    return mesh_->GetNE();
  }

  int num_boundary_elements() const {
    return mesh_->GetNBE();
  }

  // count the number of faces with the attribute "volume"
  int num_volume_faces(MeshID volume) const override {
    fatal_error("MfemMeshManager::num_volume_faces() not implemented yet");
  }

  int num_surface_faces(MeshID surface) const override {
    fatal_error("MfemMeshManager::num_surface_faces() not implemented yet");
  }

  // get all of the elements in this volume
  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const override;

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  // see Mesh::GetElementVertices
  virtual std::vector<Vertex> element_vertices(MeshID element) const override;
  std::vector<Vertex> bdr_element_vertices(MeshID element) const;

  // this one is very easy - Mesh::GetFaceVertices returns the coords of face i at the elment level
  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override;

  // The table works wonders for this
  virtual MeshID adjacent_element(MeshID element, int face) const override;

  virtual MeshID get_boundary_face_element(MeshID face) const override {
    fatal_error("MfemMeshManager::get_boundary_face_element() not implemented yet");
  }

  virtual Sense surface_sense(MeshID surface, MeshID volume) const override {
    fatal_error("MfemMeshManager::surface_sense() not implemented yet");
  }

  // mesh_->GetElement(0)->GetGeometryType()
  virtual SurfaceElementType get_surface_element_type(MeshID element) const override;

  virtual int num_vertices() const override {
    return mesh_->GetNV();
  }

  virtual double element_volume(MeshID element) const override {
    return mesh_->GetElementVolume(element);
  }

  virtual Vertex vertex_coordinates(MeshID vertex_id) const override {
    Vertex output;
    const mfem::real_t* vertices = mesh_->GetVertex(vertex_id);
    for (int i=0; i<mesh_->Dimension(); i++) output[i] = vertices[i];
    return output;
  }

  virtual std::vector<MeshID> element_connectivity(MeshID element) const override {
    fatal_error("MfemMeshManager::element_connectivity() not implemented yet");
  }

  virtual std::vector<MeshID> face_connectivity(MeshID face) const override {
    fatal_error("MfemMeshManager::face_connectivity() not implemented yet");
  }

  // Topology

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  // Seems like it's only used to create the implicit complement
  MeshID create_volume() override {
    fatal_error("MfemMeshManager::create_volume() not implemented yet");
  }

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override {
    fatal_error("MfemMeshManager::add_surface_to_volume() not implemented yet");
  }

  // Metadata methods
  void parse_metadata() override {
    fatal_error("MfemMeshManager::parse_metadata() not implemented yet");
  }

  // Accessors
  const std::unique_ptr<mfem::Mesh>& mfem_mesh() const {
    return mesh_;
  }

  // Data members
private:
  std::unique_ptr<mfem::Mesh> mesh_ {nullptr};

  // For each volume of the mesh, keep a set of the interior element IDs
  std::map<int, std::set<int>> volume_to_element_map_;
  
  // For each sideset of the mesh, keep a set of the boundary element IDs
  std::map<int, std::set<int>> sideset_to_element_map_;

  // map to keep track of each sideset held by a particular
  // volume
  std::map<int, std::set<int>> volumes_to_sidesets_;

  // set to capture all of the valid volumes/attributes
  // It's a set (not vector) to prevent double counting
  std::set<int> attributes_;

  int num_interior_faces_;
  int num_boundary_faces_;
};

struct MfemMeshElementFaceAccessor : public ElementFaceAccessor {
  MfemMeshElementFaceAccessor(const MfemMeshManager* mesh_manager, MeshID element) :
  ElementFaceAccessor(element), mesh_manager_(mesh_manager), element_(element) {

    // for each face, fetch the vertices of the face
    auto& mesh = mesh_manager_->mfem_mesh();

    // TODO: circumvent this method; fetch the table directly
    mfem::Array<int> ori; // don't care abour ori
    mesh->GetElementFaces(element_, faces_, ori);

    // TODO: Fix hardcoded 4 faces
    for (int f=0; f<4; f++) {
      int face_no = faces_[f];

      // pointer to the element object that defines this face
      auto face_obj = mesh->GetFace(face_no);

      mfem::Array<int> vertex_indices;
      face_obj->GetVertices(vertex_indices);

      for (int v=0; v<vertex_indices.Size(); v++) {
        face_vertices_[f][v].a = 0.0;
        const double* vertices = mesh->GetVertex( vertex_indices[v] );
        for (int d=0; d<mesh->SpaceDimension(); d++)
          face_vertices_[f][v][d] = vertices[d];
      }
    }
  }

  // TODO: Is this correct? We're getting interior vertices here
  //
  // Clarification: we are concerned with the face here. The moab
  // stores the vertices of the element, and picks the correct three
  // that correspond to this face. Why not just get the face from
  // the mesh itself? It exposes the vertices
  std::array<Vertex, 3> face_vertices(int i) const override {
    std::array<Vertex, 3> verts;

    // we have already gathered the vertices for this face.
    // copy them into the output array
    std::copy(face_vertices_[i], face_vertices_[i+1], verts.begin());

    // we need mesh_->GetFaceElementTransformations
    auto& mesh = mesh_manager_->mfem_mesh();

    // we get the face element trafo for the face we are
    // currently talking about
    int face_no     = faces_[i];
    auto face_el_tx = mesh->GetFaceElementTransformations(face_no);

    // The mfem docu implies that FaceElementTransformations::Elem1No
    // is the one that the normal vector is supposed to point out of.
    // We use this info to switch some stuff around

    if ( face_el_tx->Elem2No == element_ )
      // This element is NOT the one that the normal vector points out of.
      // switch two of the vertices around to make sure the cross product is good.
      std::swap( verts[0], verts[1] );

    return verts;
  }

  // data members
  const MfemMeshManager* mesh_manager_;

  // 4 faces, 3 vertices each
  Vertex face_vertices_[4][3];
  // indices for each of the faces on this element
  mfem::Array<int> faces_;
  MeshID element_;
};

// helper functions to convert mfem's element types to xdg
VolumeElementType GetVolumeElementTypeFromMfem( mfem::Element::Type t );
SurfaceElementType GetSurfaceElementTypeFromMfem( mfem::Element::Type t );

} // namespace xdg

#endif // include guard