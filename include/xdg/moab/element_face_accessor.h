#ifndef XDG_MOAB_ELEMENT_FACE_ACCESSOR_H
#define XDG_MOAB_ELEMENT_FACE_ACCESSOR_H

#include "xdg/element_face_accessor.h"
#include "xdg/moab/mesh_manager.h"

namespace xdg {

  struct MOABElementFaceAccessor : public ElementFaceAccessor {

    MOABElementFaceAccessor(const MOABMeshManager* mesh_manager, MeshID element) :
    ElementFaceAccessor(element), mesh_manager_(mesh_manager), element_ordering_(mesh_manager->mb_direct()->get_face_ordering(moab::MBTET)) {

      auto moab_mesh_manager = dynamic_cast<const MOABMeshManager*>(mesh_manager);
      if (!moab_mesh_manager) {
        throw std::runtime_error("MOABElementFaceAccessor requires a MOABMeshManager");
      }
      mesh_manager_ = moab_mesh_manager;
      element_coordinates_ = mesh_manager_->element_vertices(element);
    }

    std::array<Vertex, 3> face_vertices(int i) const override {
      std::array<Vertex, 3> verts;
      for (int j = 0; j < 3; j++) {
        verts[j] = element_coordinates_[element_ordering_[i][j]];
      }
      return std::move(verts);
    }

    // data members
    const MOABMeshManager* mesh_manager_;
    std::vector<Vertex> element_coordinates_;
    const std::vector<std::vector<int>>& element_ordering_;
  };

} // namespace xdg

#endif // XDG_MOAB_ELEMENT_FACE_ACCESSOR_H