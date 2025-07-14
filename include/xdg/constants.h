#ifndef _XDG_CONSTANTS
#define _XDG_CONSTANTS

#include <map>
#include <limits>
#include <string>

#include "math/VectorTypes.h"

// extending the math namespace from GPRT/VectorTypes.h
namespace math
{
  using double1 = vector<double, 1>;
  using double2 = vector<double, 2>;
  using double3 = vector<double, 3>;
  using double4 = vector<double, 4>;
}

// Type aliases for convenience
using double1 = math::double1;
using double2 = math::double2;
using double3 = math::double3; 
using double4 = math::double4;

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
  INTERNAL = 0,
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
  {MeshLibrary::INTERNAL, "INTERNAL"},
  {MeshLibrary::MOAB, "MOAB"},
  {MeshLibrary::LIBMESH, "LIBMESH"}
};

static const std::map<RTLibrary, std::string> RT_LIB_TO_STR =
{
  {RTLibrary::EMBREE, "EMBREE"},
  {RTLibrary::GPRT, "GPRT"},
};

// Mesh identifer type
using MeshID = int32_t;

// Invalid
constexpr MeshID ID_NONE {-1};

// Scene/Tree ID
using TreeID = int32_t;

// GPRT miss ID 
constexpr uint32_t XDG_GPRT_INVALID_GEOMETRY_ID {~0u};

// Invalid
constexpr TreeID TREEID_NONE {-1};

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

//
enum class HitOrientation { ANY, EXITING, ENTERING };

// Enumerator for different element types (maybe we want more here?)
enum class SurfaceElementType {
  TRI = 0,
  QUAD = 1,
};

enum class VolumeElementType {
  TET = 0,
  HEX = 1,
};

enum class FloatingPointPrecision {
  SINGLE = 0, // Single precision (float)
  DOUBLE = 1  // Double precision (double)
};

} // namespace xdg

#endif // include guard