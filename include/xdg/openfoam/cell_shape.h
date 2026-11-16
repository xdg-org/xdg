#ifndef _XDG_OPENFOAM_CELL_SHAPE
#define _XDG_OPENFOAM_CELL_SHAPE

#include <cstdint>
#include <string>
#include <vector>

#include "xdg/constants.h"
#include "xdg/openfoam/poly_mesh.h"
#include "xdg/openfoam/poly_mesh_topology.h"

namespace xdg {

//! \brief Classify a cell and place its vertices in XDG's canonical order
//!
//! OpenFOAM stores a cell implicitly, as whichever faces happen to name it, and
//! never as an ordered vertex list. XDG's containment tests index a fixed
//! ordering, so the shape has to be recognized and the vertices arranged to
//! match. This is the one piece of the OpenFOAM backend with no counterpart in
//! the mesh-library backends, which get ordered connectivity for free.
//!
//! The ordering produced is the usual one for each shape. For a hexahedron,
//! vertices 0-3 walk one face and 4-7 walk the opposite face, with vertex 4
//! across the cell from vertex 0, matching the face table the containment test
//! uses. For a tetrahedron, 0-2 walk one face and 3 is the remaining vertex.
//! Both come out with positive volume.
//!
//! \param mesh The mesh the cell belongs to
//! \param topology Derived cell-to-face connectivity for that mesh
//! \param cell The cell to classify
//! \param nodes Receives the ordered vertex indices: 4 for a tetrahedron and 8
//!        for a hexahedron. Left empty for an unsupported shape.
//! \return The element type, or VolumeElementType::UNSUPPORTED
VolumeElementType cell_connectivity(const PolyMesh& mesh,
                                    const PolyMeshTopology& topology,
                                    int32_t cell,
                                    std::vector<int32_t>& nodes);

//! \brief Describe a cell's face makeup for use in diagnostics
//! \return A phrase such as "cell with 5 faces (2 with 3 vertices, 3 with 4
//!         vertices) and 6 vertices", which is how a prism reads
std::string cell_shape_description(const PolyMesh& mesh,
                                   const PolyMeshTopology& topology,
                                   int32_t cell);

} // namespace xdg

#endif // include guard
