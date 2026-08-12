#include "xdg/error.h"

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

  double face_area_from_vertices(const std::vector<Vertex>& vertices) {
    if (vertices.size() == 3) {
      return triangle_area(vertices[0], vertices[1], vertices[2]);
    }
    if (vertices.size() == 4) {
      return triangle_area(vertices[0], vertices[1], vertices[2]) +
             triangle_area(vertices[0], vertices[2], vertices[3]);
    }
    fatal_error("Face has unsupported vertex count {}", vertices.size());
    return 0.0;
  }

  double face_volume_contribution_from_vertices(const std::vector<Vertex>& vertices) {
    if (vertices.size() == 3) {
      return triangle_volume_contribution(vertices[0], vertices[1], vertices[2]);
    }
    if (vertices.size() == 4) {
      return triangle_volume_contribution(vertices[0], vertices[1], vertices[2]) +
             triangle_volume_contribution(vertices[0], vertices[2], vertices[3]);
    }
    fatal_error("Face has unsupported vertex count {}", vertices.size());
    return 0.0;
  }

} // namespace xdg