#ifndef XDG_ELEMENT_FACE_ACCESSOR_H
#define XDG_ELEMENT_FACE_ACCESSOR_H

#include "xdg/mesh_manager_interface.h"

namespace xdg {

  // this class exists to provide a common interface for accessing
  // the faces and face vertices of an element
  struct ElementFaceAccessor {
    ElementFaceAccessor(MeshID element)
      : element_(element) {}

    static std::shared_ptr<ElementFaceAccessor> create(const MeshManager* mesh_manager, MeshID element);

    virtual std::array<Vertex, 3> face_vertices(int i) const = 0;

    MeshID element() const { return element_; }

    // data members
    MeshID element_;
  };


} // namespace xdg

#endif // XDG_ELEMENT_FACE_ACCESSOR_H