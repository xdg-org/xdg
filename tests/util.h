#include <random>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "xdg/constants.h"
#include "xdg/ray_tracers.h"
#include "xdg/mesh_managers.h"
#include "vulkan_probe.h"

namespace xdg::test {

using MOAB_Interface = std::integral_constant<MeshLibrary, MeshLibrary::MOAB>;
using LibMesh_Interface = std::integral_constant<MeshLibrary, MeshLibrary::LIBMESH>;

using Embree_Raytracer = std::integral_constant<RTLibrary, RTLibrary::EMBREE>;
using GPRT_Raytracer = std::integral_constant<RTLibrary, RTLibrary::GPRT>;

} // namespace xdg::test

namespace Catch {

template<typename MeshTag, typename RayTag>
struct StringMaker<std::pair<MeshTag, RayTag>> {
  static std::string convert(std::pair<MeshTag, RayTag>) {
    return fmt::format("{}/{}", xdg::MESH_LIB_TO_STR.at(MeshTag::value),
                       xdg::RT_LIB_TO_STR.at(RayTag::value));
  }
};

} // namespace Catch

// Library availability checks

inline void check_ray_tracer_supported(xdg::RTLibrary rt) {
  #ifndef XDG_ENABLE_EMBREE
  if (rt == xdg::RTLibrary::EMBREE) {
    SKIP("XDG not built with Embree backend; skipping Embree tests.");
  }
  #endif

  #ifndef XDG_ENABLE_GPRT
  if (rt == xdg::RTLibrary::GPRT) {
    SKIP("XDG not built with GPRT backend; skipping GPRT tests.");
  }
  #else // XDG_ENABLE_GPRT
  if (rt == xdg::RTLibrary::GPRT && !system_has_vk_device()) {
    SKIP("No Vulkan device found; skipping GPRT tests.");
  }
  #endif
}

inline void check_mesh_library_supported(xdg::MeshLibrary mesh) {
  #ifndef XDG_ENABLE_MOAB
  if (mesh == xdg::MeshLibrary::MOAB) {
    SKIP("MOAB backend not built; skipping.");
  }
  #endif

  #ifndef XDG_ENABLE_LIBMESH
  if (mesh == xdg::MeshLibrary::LIBMESH) {
    SKIP("LibMesh backend not built; skipping.");
  }
  #endif
}

// Factories

inline std::unique_ptr<xdg::MeshManager>
create_mesh_manager(xdg::MeshLibrary mesh) {
  #ifdef XDG_ENABLE_MOAB
  if (mesh == xdg::MeshLibrary::MOAB)
    return std::make_unique<xdg::MOABMeshManager>();
  #endif

  #ifdef XDG_ENABLE_LIBMESH
  if (mesh == xdg::MeshLibrary::LIBMESH)
    return std::make_unique<xdg::LibMeshManager>();
  #endif

  return nullptr;
}

inline std::shared_ptr<xdg::RayTracer>
create_raytracer(xdg::RTLibrary rt) {
  #ifdef XDG_ENABLE_EMBREE
  if (rt == xdg::RTLibrary::EMBREE)
    return std::make_shared<xdg::EmbreeRayTracer>();
  #endif

  #ifdef XDG_ENABLE_GPRT
  if (rt == xdg::RTLibrary::GPRT)
    return std::make_shared<xdg::GPRTRayTracer>();
  #endif

  return nullptr;
}

inline void make_rays(size_t N, std::vector<xdg::Position>& origins, std::vector<xdg::Direction>& directions)
{
  origins.clear();
  directions.clear();
  origins.reserve(N);
  directions.reserve(N);
  for (size_t i = 0; i < N; ++i) {
    int axis = static_cast<int>(i % 3);
    double s = (i % 2) ? 1.0 : -1.0;
    origins.push_back({0.0, 0.0, 0.0});
    if (axis == 0) directions.push_back({s, 0.0, 0.0});
    else if (axis == 1) directions.push_back({0.0, s, 0.0});
    else directions.push_back({0.0, 0.0, s});
  }
}

inline void make_points(size_t N, std::vector<xdg::Position>& points, std::vector<xdg::Direction>& directions)
{
  points.resize(N);
  directions.resize(N);
  for (size_t i = 0; i < N; ++i) {
    // evens inside (origin), odds just outside +X
    points[i] = (i % 2 == 0) ? xdg::Position{0.0, 0.0, 0.0} : xdg::Position{5.1, 0.0, 0.0};
    // alternate ±X directions
    directions[i] = (i % 2 == 0) ? xdg::Direction{1.0, 0.0, 0.0}
                                 : xdg::Direction{-1.0, 0.0, 0.0};
  }
}
