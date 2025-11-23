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

  // Finalize the mesh setup
  mesh_->FinalizeTopology();
}

} // namespace xdg