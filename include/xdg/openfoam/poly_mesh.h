#ifndef _XDG_OPENFOAM_POLY_MESH
#define _XDG_OPENFOAM_POLY_MESH

#include <cstdint>
#include <string>
#include <vector>

#include "xdg/vec3da.h"

namespace xdg {

//! \brief A named, contiguous run of boundary faces
//!
//! Patch faces are stored after all internal faces and are contiguous, so a
//! patch is a range rather than a set.
struct PolyMeshPatch {
  std::string name;
  std::string type;       //!< OpenFOAM patch type, e.g. patch, wall, empty
  int32_t start_face {0};
  int32_t n_faces {0};
  std::vector<std::string> groups; //!< the patch's inGroups, if any
};

//! \brief A named set of cells, used to carry material assignment
struct PolyMeshCellZone {
  std::string name;
  std::vector<int32_t> cells;
};

//! \brief The raw contents of an OpenFOAM `constant/polyMesh` directory
//!
//! This is a faithful transcription of the files rather than an interpretation
//! of them: faces are stored in the compressed-row layout OpenFOAM itself uses
//! for binary output, and cells remain implicit in \ref owner and
//! \ref neighbour. Deriving cell-to-face connectivity, volumes, surfaces and
//! senses from this is the mesh manager's job.
struct PolyMesh {
  std::vector<Vertex> points;

  //! Offsets into \ref face_points, of length `num_faces() + 1`
  std::vector<int32_t> face_offsets;
  //! Point indices for every face, concatenated
  std::vector<int32_t> face_points;

  //! Owning cell of each face, of length `num_faces()`
  std::vector<int32_t> owner;
  //! Neighboring cell of each internal face, of length \ref n_internal_faces
  std::vector<int32_t> neighbour;

  std::vector<PolyMeshPatch> patches;
  std::vector<PolyMeshCellZone> cell_zones;

  int32_t n_cells {0};
  int32_t n_internal_faces {0};

  int32_t num_faces() const
  {
    return face_offsets.empty() ? 0 : static_cast<int32_t>(face_offsets.size()) - 1;
  }

  int32_t num_points() const { return static_cast<int32_t>(points.size()); }

  //! \brief Number of points bounding face \p face
  int32_t face_size(int32_t face) const
  {
    return face_offsets[face + 1] - face_offsets[face];
  }

  //! \brief Point indices bounding face \p face, in perimeter order
  //! \note The winding is such that the face normal points from the owner cell
  //!       into the neighbour cell.
  const int32_t* face_vertices(int32_t face) const
  {
    return face_points.data() + face_offsets[face];
  }

  //! \brief Whether \p face lies on a boundary patch rather than between cells
  bool is_boundary_face(int32_t face) const { return face >= n_internal_faces; }
};

//! \brief Read an OpenFOAM mesh from disk
//! \param path Either a case directory or a polyMesh directory
//! \return The parsed mesh, with coordinates as written (OpenFOAM uses meters)
PolyMesh read_poly_mesh(const std::string& path);

} // namespace xdg

#endif // include guard
