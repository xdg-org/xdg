#ifndef _XDG_AREA_H
#define _XDG_AREA_H

#include "xdg/vec3da.h"

namespace xdg {

double triangle_volume_contribution(const std::array<Vertex, 3>& vertices);
double triangle_volume_contribution(const Vertex& v0, const Vertex& v1, const Vertex& v2);

double triangle_area(const std::array<Vertex, 3>& vertices);
double triangle_area(const Vertex& v0, const Vertex& v1, const Vertex& v2);

double tetrahedron_volume(const std::array<Vertex, 4>& vertices);

}

#endif // include guard