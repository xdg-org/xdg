#ifndef _XDG_AREA_H
#define _XDG_AREA_H

#include "xdg/vec3da.h"

namespace xdg {

double triangle_volume_contribution(const std::array<Vertex, 3>& vertices);
double triangle_volume_contribution(const Vertex& v0, const Vertex& v1, const Vertex& v2);

double triangle_area(const std::array<Vertex, 3>& vertices);
double triangle_area(const Vertex& v0, const Vertex& v1, const Vertex& v2);

double face_area_from_vertices(const std::vector<Vertex>& vertices);
double face_volume_contribution_from_vertices(const std::vector<Vertex>& vertices);

} // namespace xdg

#endif // include guard