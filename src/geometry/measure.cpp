#include "xdg/geometry/measure.h"

namespace xdg {
  double triangle_volume_contribution(const std::array<Vertex, 3>& vertices)
  {
    return triangle_volume_contribution(vertices[0], vertices[1], vertices[2]);
  }

  double triangle_volume_contribution(const Vertex& v0, const Vertex& v1, const Vertex& v2)
  {
    return v0.dot((v1-v0).cross(v2-v0));
  }

  double triangle_area(const std::array<Vertex, 3>& vertices)
  {
    return triangle_area(vertices[0], vertices[1], vertices[2]);
  }

  double triangle_area(const Vertex& v0, const Vertex& v1, const Vertex& v2)
  {
    return 0.5 * (v1-v0).cross(v2-v0).length();
  }

  double tetrahedron_volume(const std::array<Vertex, 4>& vertices)
  {
    return std::abs(((vertices[1] - vertices[0]).cross(vertices[2] - vertices[0])).dot(vertices[3] - vertices[0])) / 6.0;
  }
}