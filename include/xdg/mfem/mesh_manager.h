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
  MfemMeshManager();

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

  BoundingBox element_bounding_box(MeshID element) const override {
    mesh_.Get
  }

  // Data members
private:
  std::unique_ptr<mfem::Mesh> mesh_ {nullptr};
};

}

#endif // include guard