#ifndef _XDG_CONSTANTS
#define _XDG_CONSTANTS

#include <cmath>
#include <map>
#include <limits>
#include <string>

#include "fmt/format.h"

#include "xdg/shared_enums.h"

namespace xdg {

constexpr double INFTY {std::numeric_limits<double>::max()};

#ifdef XDG_DEBUG
  // from Embree, if the floating point value of Tfar is larger than this value,
  // it is considered overflow by the internal hit verification function. This
  // is only enabled when Embree is compiled in debug mode. I think a
  // corresponding behavior makes sense to keep here for now
  constexpr double INFTYF {1.844E18f};
#else
  constexpr double INFTYF {std::numeric_limits<float>::max()};
#endif

inline const double DILATION_FACTOR {std::pow(10, -std::numeric_limits<float>::digits10)};

// TODO : Consider this as an option for managing missed hits?
constexpr double PLUCKER_ZERO_TOL {20 * std::numeric_limits<double>::epsilon()};

constexpr double TINY_BIT {1e-10};

// Whether information pertains to a surface or volume
enum class GeometryType {
 SURFACE = 2,
  VOLUME = 3
};

// Surface to Volume sense values (may differ from mesh-specific values)
enum class Sense {
  UNSET = -1,
  FORWARD = 0,
  REVERSE = 1
};

// Mesh library identifier
enum class MeshLibrary {
  MOCK = 0, // mock testing interface
  MOAB,
  LIBMESH
};

// Ray Tracing library identifier
enum class RTLibrary {
  EMBREE,
  GPRT
};

static const std::map<MeshLibrary, std::string> MESH_LIB_TO_STR =
{
  {MeshLibrary::MOCK, "MOCK"},
  {MeshLibrary::MOAB, "MOAB"},
  {MeshLibrary::LIBMESH, "LIBMESH"}
};

static const std::map<RTLibrary, std::string> RT_LIB_TO_STR =
{
  {RTLibrary::EMBREE, "EMBREE"},
  {RTLibrary::GPRT, "GPRT"}
};

// Mesh identifer type
using MeshID = int32_t;

// Null mesh ID
constexpr MeshID ID_NONE {-1};

// Scene/Tree ID
using TreeID = int32_t;
using SurfaceTreeID = TreeID;
using ElementTreeID = TreeID;

// Null tree ID
constexpr TreeID TREE_NONE {-1};

// for abs(x) >= min_rcp_input the newton raphson rcp calculation does not fail
constexpr float min_rcp_input = std::numeric_limits<float>::min() /* FIX ME */ *1E5 /* SHOULDNT NEED TO MULTIPLY BY THIS VALUE */;
constexpr int BVH_MAX_DEPTH = 64;

// geometric property type (e.g. material assignment or boundary condition)
// TODO: separate into VolumeProperty and SurfaceProperty
enum class PropertyType {
  BOUNDARY_CONDITION = -1,
  MATERIAL = 0,
  DENSITY = 1,
  TEMPERATURE = 2
};

static const std::map<PropertyType, std::string> PROP_TYPE_TO_STR =
{
  {PropertyType::BOUNDARY_CONDITION, "BOUNDARY_CONDITION"},
  {PropertyType::MATERIAL, "MATERIAL"},
  {PropertyType::DENSITY, "DENSITY"},
  {PropertyType::TEMPERATURE, "TEMPERATURE"}
};

struct Property{
  PropertyType type;
  std::string value;
};

static Property VOID_MATERIAL {PropertyType::MATERIAL, "void"};

// Enumerator for different ray fire types
enum class RayFireType { VOLUME, POINT_CONTAINMENT, ACCUMULATE_HITS, FIND_VOLUME };

// Enumerator for different element types (maybe we want more here?)
enum class SurfaceElementType {
  TRI = 0,
  QUAD = 1,
};

enum class VolumeElementType {
  TET = 0,
  HEX = 1,
};

} // namespace xdg

namespace fmt {
  template <>
struct formatter<xdg::RTLibrary> : fmt::formatter<std::string> {
  auto format(xdg::RTLibrary lib, fmt::format_context& ctx) const {
    return fmt::formatter<std::string>::format(xdg::RT_LIB_TO_STR.at(lib), ctx);
  }
};

template <>
struct formatter<xdg::MeshLibrary> : fmt::formatter<std::string> {
  auto format(xdg::MeshLibrary lib, fmt::format_context& ctx) const {
    return fmt::formatter<std::string>::format(xdg::MESH_LIB_TO_STR.at(lib), ctx);
  }
};


}

#endif // include guard