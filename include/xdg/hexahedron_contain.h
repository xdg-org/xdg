#ifndef XDG_HEXAHEDRON_CONTAIN_H
#define XDG_HEXAHEDRON_CONTAIN_H

#include <array>

#include "xdg/vec3da.h"

// Forward declarations for Embree callback argument types
struct RTCIntersectFunctionNArguments;
struct RTCOccludedFunctionNArguments;

namespace xdg {

/**
 * @brief Determines if a point is inside or on the boundary of a hexahedron
 * using MBCN canonical Hex8 ordering (Tautges 2010).
 *
 * The hexahedron is defined by eight vertices ordered per the MBCN
 * canonical numbering. The containment test uses face-plane sign tests; points
 * on the boundary are considered inside.
 */
bool hex_containment_test(const Position& point,
                          const std::array<Vertex, 8>& verts);

// Embree call back functions for element search
void HexahedronIntersectionFunc(RTCIntersectFunctionNArguments* args);
void HexahedronOcclusionFunc(RTCOccludedFunctionNArguments* args);

} // namespace xdg

#endif // include guard
