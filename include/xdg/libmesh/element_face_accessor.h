#ifndef XDG_LIBMESH_ELEMENT_FACE_ACCESSOR_H
#define XDG_LIBMESH_ELEMENT_FACE_ACCESSOR_H

#include "xdg/element_face_accessor.h"
#include "xdg/libmesh/mesh_manager.h"

#include "libmesh/elem.h"
#include "libmesh/cell_tet4.h"


namespace xdg {

  struct LibMeshElementFaceAccessor : public ElementFaceAccessor {

    LibMeshElementFaceAccessor(const LibMeshManager* mesh_manager, MeshID element) :
    ElementFaceAccessor(element), mesh_manager_(mesh_manager) {
      mesh_ = mesh_manager_->mesh();
      elem_ptr_ = mesh_->elem_ptr(element);
      tet_ = (const libMesh::Tet4*)elem_ptr_;
    }

    std::array<Vertex, 3> face_vertices(int i) const override {
      std::array<Vertex, 3> coords;
      for (int j = 0; j < 3; j++) {
        const auto node_ptr = elem_ptr_->node_ptr(tet_->side_nodes_map[i][j]);
        coords[j] = {(*node_ptr)(0), (*node_ptr)(1), (*node_ptr)(2)};
      }
      return std::move(coords);
    }

    // data members
    const LibMeshManager* mesh_manager_;
    const libMesh::MeshBase* mesh_;
    const libMesh::Tet4* tet_;
    const libMesh::Elem* elem_ptr_;
  };

} // namespace xdg

#endif // XDG_LIBMESH_ELEMENT_FACE_ACCESSOR_H