#ifndef _XDG_TOOLS_FMT_FORMATTERS_H
#define _XDG_TOOLS_FMT_FORMATTERS_H

#include <string>

#include <fmt/format.h>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/vec3da.h"

namespace fmt {

template<>
struct formatter<xdg::Vec3da> : formatter<std::string> {
  template<typename FormatContext>
  auto format(const xdg::Vec3da& value, FormatContext& ctx) const
  {
    return formatter<std::string>::format(
      fmt::format("({}, {}, {})", value.x, value.y, value.z), ctx);
  }
};

template<>
struct formatter<xdg::BoundingBox> {
  template<typename ParseContext>
  constexpr auto parse(ParseContext& ctx)
  {
    return ctx.begin();
  }

  template<typename FormatContext>
  auto format(const xdg::BoundingBox& box, FormatContext& ctx) const
  {
    return fmt::format_to(
      ctx.out(), "[Lower left: [{}, {}, {}], Upper right: [{}, {}, {}]]",
      box.min_x, box.min_y, box.min_z,
      box.max_x, box.max_y, box.max_z);
  }
};

template<>
struct formatter<xdg::RTLibrary> : formatter<std::string> {
  auto format(xdg::RTLibrary lib, format_context& ctx) const
  {
    return formatter<std::string>::format(xdg::RT_LIB_TO_STR.at(lib), ctx);
  }
};

template<>
struct formatter<xdg::MeshLibrary> : formatter<std::string> {
  auto format(xdg::MeshLibrary lib, format_context& ctx) const
  {
    return formatter<std::string>::format(xdg::MESH_LIB_TO_STR.at(lib), ctx);
  }
};

template<>
struct formatter<xdg::SurfaceFaceType> : formatter<std::string> {
  auto format(xdg::SurfaceFaceType type, format_context& ctx) const
  {
    return formatter<std::string>::format(xdg::SURFACE_FACE_TYPE_TO_STR.at(type), ctx);
  }
};

template<>
struct formatter<xdg::VolumeElementType> : formatter<std::string> {
  auto format(xdg::VolumeElementType type, format_context& ctx) const
  {
    return formatter<std::string>::format(xdg::VOLUME_ELEMENT_TYPE_TO_STR.at(type), ctx);
  }
};

} // namespace fmt

#endif // _XDG_TOOLS_FMT_FORMATTERS_H
