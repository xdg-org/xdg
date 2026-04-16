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

  int num_volumes() const override {
    return volumes_.size();
  }

  int num_surfaces() const override {
    return surfaces_.size();
  }

  int num_ents_of_dimension(int dim) const override {
    switch (dim) {
      case 3: return num_volumes();
      case 2: return num_surfaces();
      default: return 0;
    }
  }

  int num_volume_elements(MeshID volume) const override {
    fatal_error("MfemMeshManager::num_volume_elements() not implemented yet");
  }

  int num_volume_elements() const override {
    return mesh_->GetNE();
  }

  int num_volume_faces(MeshID volume) const override {
    fatal_error("MfemMeshManager::num_volume_faces() not implemented yet");
  }

  int num_surface_faces(MeshID surface) const override {
    fatal_error("MfemMeshManager::num_surface_faces() not implemented yet");
  }

  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const override {
    fatal_error("MfemMeshManager::get_volume_elements() not implemented yet");
  }

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const override {
    fatal_error("MfemMeshManager::get_surface_faces() not implemented yet");
  }

  virtual std::vector<Vertex> element_vertices(MeshID element) const override {
    fatal_error("MfemMeshManager::element_vertices() not implemented yet");
  }

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override {
    fatal_error("MfemMeshManager::face_vertices() not implemented yet");
  }

  virtual MeshID adjacent_element(MeshID element, int face) const override {
    fatal_error("MfemMeshManager::adjacent_element() not implemented yet");
  }

  virtual Sense surface_sense(MeshID surface, MeshID volume) const override {
    fatal_error("MfemMeshManager::surface_sense() not implemented yet");
  }

  virtual SurfaceElementType get_surface_element_type(MeshID element) const override {
    fatal_error("MfemMeshManager::get_surface_element_type() not implemented yet");
  }

  virtual int num_vertices() const override {
    fatal_error("MfemMeshManager::num_vertices() not implemented yet");
  }

  virtual double element_volume(MeshID element) const override {
    fatal_error("MfemMeshManager::element_volume() not implemented yet");
  }

  virtual Vertex vertex_coordinates(MeshID vertex_id) const override {
    fatal_error("MfemMeshManager::vertex_coordinates() not implemented yet");
  }

  virtual std::vector<MeshID> element_connectivity(MeshID element) const override {
    fatal_error("MfemMeshManager::element_connectivity() not implemented yet");
  }

  virtual std::vector<MeshID> face_connectivity(MeshID face) const override {
    fatal_error("MfemMeshManager::face_connectivity() not implemented yet");
  }

  // BoundingBox _mfem_element_bounding_box(mfem::Element* elem) const {
  //   auto bbox = BoundingBox {
  //     INFTY, INFTY, INFTY,
  //     -INFTY, -INFTY, -INFTY
  //   };
  //   for(int i = 0; i < elem->GetNVertices(); i++) {
  //     const mfem::real_t* v = mesh_->GetVertex(*(i + elem->GetVertices()));
  //     bbox.update(v);
  //   }
  //   return bbox;
  // }

  // BoundingBox element_bounding_box(MeshID element) const {
  //   auto elem = mesh_->GetElement(element);
  //   if (!elem) {
  //     fatal_error(fmt::format("MfemMeshManager::element_bounding_box(): invalid element ID {}", element));
  //   }
  //   return _mfem_element_bounding_box(elem);
  // }

  // Topology
  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override {
    fatal_error("MfemMeshManager::get_volume_surfaces() not implemented yet");
  }

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override {
    fatal_error("MfemMeshManager::surface_senses() not implemented yet");
  }

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
};

}

#endif // include guard