#ifndef XDG_ELEMENT_FACE_ACCESSOR_H
#define XDG_ELEMENT_FACE_ACCESSOR_H

#include "xdg/mesh_manager_interface.h"

namespace xdg {

  //! \brief Class to provide access to face vertices of mesh elements
  //! \details This object provides an abstraction layer for accessing the vertices
  //! of element faces across different mesh libraries. It allows consistent access
  //! to face geometry while hiding the underlying mesh representation details, allowing
  //! higher level operations like element traversal to rely on the same implementation.
  //! It works by binding to a mesh manager and element ID, and then providing access to
  //! the face vertices of the element via the library-specific mesh_manager implementation.
  struct ElementFaceAccessor {
    ElementFaceAccessor(MeshID element)
      : element_(element) {}

    //! \brief Create an ElementFaceAccessor for a given element
    //! \param mesh_manager The mesh manager to use
    //! \param element The element to create the accessor for
    //! \return A shared pointer to the ElementFaceAccessor
    static std::shared_ptr<ElementFaceAccessor> create(const MeshManager* mesh_manager, MeshID element);

    //! \brief Get the vertices of a face
    //! \param i The face index (0-3 for tetrahedra)
    //! \return An array of vertices
    virtual std::array<Vertex, 3> face_vertices(int i) const = 0;

    //! \brief Get the element ID
    //! \return The element ID
    MeshID element() const { return element_; }

    // data members
    MeshID element_;
  };


} // namespace xdg

#endif // XDG_ELEMENT_FACE_ACCESSOR_H