#ifndef _XDG_BOUNDING_BOX_H
#define _XDG_BOUNDING_BOX_H

#include <fmt/format.h>

#include "xdg/constants.h"
#include "xdg/vec3da.h"
namespace xdg {

union BoundingBox {
struct {
  double min_x {0.0};
  double min_y {0.0};
  double min_z {0.0};
  double max_x {0.0};
  double max_y {0.0};
  double max_z {0.0};
};
double bounds[6];

double operator[](const size_t index) const {
  return bounds[index];
}

bool operator ==(const BoundingBox& other) {
  return min_x == other.min_x &&
         min_y == other.min_y &&
         min_z == other.min_z &&
         max_x == other.max_x &&
         max_y == other.max_y &&
         max_z == other.max_z;
}

void update(const Vertex& v) {
  min_x = std::min(min_x, v.x);
  min_y = std::min(min_y, v.y);
  min_z = std::min(min_z, v.z);
  max_x = std::max(max_x, v.x);
  max_y = std::max(max_y, v.y);
  max_z = std::max(max_z, v.z);
}

void update(const std::array<double, 3>& v) {
  min_x = std::min(min_x, v[0]);
  min_y = std::min(min_y, v[1]);
  min_z = std::min(min_z, v[2]);
  max_x = std::max(max_x, v[0]);
  max_y = std::max(max_y, v[1]);
  max_z = std::max(max_z, v[2]);
}

void update(const BoundingBox& other) {
  min_x = std::min(min_x, other.min_x);
  min_y = std::min(min_y, other.min_y);
  min_z = std::min(min_z, other.min_z);
  max_x = std::max(max_x, other.max_x);
  max_y = std::max(max_y, other.max_y);
  max_z = std::max(max_z, other.max_z);
}

Position center() const {
  return Position {(min_x + max_x), (min_y + max_y), (min_z + max_z)} * 0.5;
}

Vec3da width() const {
  return Vec3da {max_x - min_x, max_y - min_y, max_z - min_z};
}

Position lower_left() const {
  return {min_x, min_y, min_z};
}

Position upper_right() const {
  return {max_x, max_y, max_z};
}

bool contains(const Position& p) const {
  return p.x >= min_x && p.x <= max_x &&
         p.y >= min_y && p.y <= max_y &&
         p.z >= min_z && p.z <= max_z;
}

double max_chord_length() const {
  Vec3da w = width();
  return  std::sqrt(w.dot(w));
}

/**
 * @brief Returns a distance by which the box should be dilated to account for mixed precision effects
 *
 * This method calculates a small distance that can be used to dilate the bounding box
 * to account for potential numerical precision issues when working with mixed precision
 * calculations. The distance is based on the maximum chord length of the box and the
 * precision of float values.
 *
 * @return double The dilation distance
 */
double dilation() const {
  return max_chord_length() * DILATION_FACTOR;
}

template <typename T>
static BoundingBox from_points(const T& points) {
  BoundingBox bbox {INFTY, INFTY, INFTY, -INFTY, -INFTY, -INFTY};
  for (const auto& p : points) {
    bbox.update(p);
  }
  return bbox;
}

};

inline std::ostream& operator <<(std::ostream& os, const BoundingBox& bbox) {
  os << "Lower left: " << bbox.min_x << ", " << bbox.min_y << ", " << bbox.min_z << ", "
     << "Upper right: " << bbox.max_x << ", " << bbox.max_y << ", " << bbox.max_z;
  return os;
}

} // namespace xdg

namespace fmt {
template <>
struct formatter<xdg::BoundingBox> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const xdg::BoundingBox& box, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "[Lower left: {}, Upper right: {}]",
            fmt::format("[{}, {}, {}]", box.min_x, box.min_y, box.min_z),
            fmt::format("[{}, {}, {}]", box.max_x, box.max_y, box.max_z));
    }
};

}

#endif // include guard