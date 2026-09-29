#ifndef XDG_GEOMETRY_HEXAHEDRON_CONTAIN_H
#define XDG_GEOMETRY_HEXAHEDRON_CONTAIN_H

#include <array>

#include "xdg/vec3da.h"

namespace xdg {

/**
 * @brief Determines if a point is inside or on the boundary of a hexahedron
 * using MBCN canonical Hex8 ordering (Tautges 2010).
 *
 * The hexahedron is defined by eight vertices ordered per the MBCN
 * canonical numbering. The containment test casts rays toward the hexahedron
 * centroid and uses Plucker edge-coordinate sign tests against triangulated
 * faces; points on the boundary are considered inside.
 */
bool hex_containment_test(const Position& point,
                          const std::array<Vertex, 8>& verts);

} // namespace xdg

#endif // include guard
