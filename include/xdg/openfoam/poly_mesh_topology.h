#ifndef _XDG_OPENFOAM_POLY_MESH_TOPOLOGY
#define _XDG_OPENFOAM_POLY_MESH_TOPOLOGY

#include <cstdint>
#include <string>
#include <vector>

#include "xdg/constants.h"
#include "xdg/openfoam/poly_mesh.h"

namespace xdg {

//! \brief A set of cells sharing a material assignment
//!
//! Volumes come from cellZones. Cells belonging to no zone are collected into
//! a single unnamed volume, which carries no material and is therefore treated
//! as void, matching how the libMesh backend handles an unnamed subdomain.
struct PolyMeshVolume {
  //! Volume ID, assigned contiguously from 1
  MeshID id {ID_NONE};
  std::string name;             //!< cellZone name, empty for unzoned cells
  //! Cells making up this volume
  std::vector<int32_t> cells;
};

//! \brief A set of faces separating two volumes, or a volume from the outside
//!
//! Surfaces are derived rather than stored: a boundary patch becomes one
//! surface per volume it touches, and every internal face whose owner and
//! neighbour fall in different volumes joins the surface for that volume pair.
struct PolyMeshSurface {
  //! Surface ID, assigned contiguously from 1
  MeshID id {ID_NONE};
  std::string name;             //!< patch name; empty for an interior interface

  //! Index into PolyMesh::patches, or -1 for an interior interface
  int32_t patch_index {-1};

  //! Faces making up this surface
  std::vector<int32_t> faces;

  //! Volume the face normals point out of
  MeshID forward {ID_NONE};
  //! Volume the face normals point into, or ID_NONE at a mesh boundary
  MeshID reverse {ID_NONE};

  //! \brief Whether this surface lies on a boundary patch
  bool is_boundary() const { return patch_index >= 0; }
};

//! \brief Volumes, surfaces and senses derived from a PolyMesh
//!
//! OpenFOAM stores exactly the object XDG's topology is built from: a face
//! with a cell on each side and a winding fixed relative to the owner. Almost
//! everything here is therefore array indexing rather than the interface
//! discovery a backend over a node-and-element mesh library has to perform.
struct PolyMeshTopology {
  //! Volumes, ordered so that index i holds the volume with ID i + 1
  std::vector<PolyMeshVolume> volumes;
  //! Surfaces, ordered so that index i holds the surface with ID i + 1
  std::vector<PolyMeshSurface> surfaces;

  //! Volume owning each cell, indexed by cell
  std::vector<MeshID> cell_volume;

  //! Surface containing each face, or ID_NONE where a face is interior to a
  //! volume and therefore not part of the geometry
  std::vector<MeshID> face_surface;

  //! Whether a face's stored winding must be reversed so that its normal points
  //! out of its surface's forward volume
  //!
  //! OpenFOAM orders internal faces so that owner < neighbour by cell index,
  //! which says nothing about which volume the owner falls in. Faces on one
  //! interface can therefore be wound either way, and the ones that disagree
  //! with the surface's forward volume are flagged here.
  std::vector<bool> face_flipped;

  //! Faces of each cell, in compressed-row form. The offset of a face within a
  //! cell's run is the local face index the MeshManager interface uses, so
  //! adjacency and face geometry stay consistent with one another.
  std::vector<int32_t> cell_face_offsets;
  //! Face indices for every cell, concatenated
  std::vector<int32_t> cell_faces;

  //! \brief Number of cells in the mesh
  int32_t num_cells() const
  {
    return cell_face_offsets.empty() ? 0
                                     : static_cast<int32_t>(cell_face_offsets.size()) - 1;
  }

  //! \brief Number of faces bounding \p cell
  int32_t num_cell_faces(int32_t cell) const
  {
    return cell_face_offsets[cell + 1] - cell_face_offsets[cell];
  }

  //! \brief Faces bounding \p cell, in local face index order
  const int32_t* cell_face_list(int32_t cell) const
  {
    return cell_faces.data() + cell_face_offsets[cell];
  }

  //! \brief Look up a volume by ID
  //! \note Volume IDs are assigned contiguously from 1
  const PolyMeshVolume& volume(MeshID id) const { return volumes.at(id - 1); }

  //! \brief Look up a surface by ID
  //! \note Surface IDs are assigned contiguously from 1
  const PolyMeshSurface& surface(MeshID id) const { return surfaces.at(id - 1); }
};

//! \brief Derive volumes, surfaces and senses from a mesh
//! \param mesh A mesh as read from a polyMesh directory
//! \return The derived topology
PolyMeshTopology build_topology(const PolyMesh& mesh);

} // namespace xdg

#endif // include guard
