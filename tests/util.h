#include <random>
#include <string>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "xdg/constants.h"
#include "xdg/ray_tracers.h"
#include "xdg/mesh_managers.h"
#include "xdg/available_device_probe.h"

namespace xdg::test {

using MOAB_Interface = std::integral_constant<MeshLibrary, MeshLibrary::MOAB>;
using LibMesh_Interface = std::integral_constant<MeshLibrary, MeshLibrary::LIBMESH>;

using Embree_Raytracer = std::integral_constant<RTLibrary, RTLibrary::EMBREE>;
using GPRT_Raytracer = std::integral_constant<RTLibrary, RTLibrary::GPRT>;
using CuBQL_Raytracer = std::integral_constant<RTLibrary, RTLibrary::CUBQL>;
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

inline bool ray_tracer_available(xdg::RTLibrary rt) {
  switch (rt) {
    case xdg::RTLibrary::EMBREE:
    #ifdef XDG_ENABLE_EMBREE
      return true;
    #else
      return false;
    #endif

    case xdg::RTLibrary::GPRT:
    #ifdef XDG_ENABLE_GPRT
      return system_has_vk_device();
    #else
      return false;
    #endif

    case xdg::RTLibrary::CUBQL:
    #ifdef XDG_ENABLE_CUBQL
      return system_has_omp_target_device();
    #else
      return false;
    #endif
  }

  return false;
}

inline bool mesh_library_available(xdg::MeshLibrary mesh) {
  switch (mesh) {
    case xdg::MeshLibrary::MOCK:
      return true;

    case xdg::MeshLibrary::MOAB:
    #ifdef XDG_ENABLE_MOAB
      return true;
    #else
      return false;
    #endif

    case xdg::MeshLibrary::LIBMESH:
    #ifdef XDG_ENABLE_LIBMESH
      return true;
    #else
      return false;
    #endif
  }

  return false;
}

inline void check_ray_tracer_supported(xdg::RTLibrary rt) {
  if (!ray_tracer_available(rt)) {
    SKIP(fmt::format("{} backend unavailable; skipping.", xdg::RT_LIB_TO_STR.at(rt)));
  }
}

inline void check_mesh_library_supported(xdg::MeshLibrary mesh) {
  if (!mesh_library_available(mesh)) {
    SKIP(fmt::format("{} backend unavailable; skipping.", xdg::MESH_LIB_TO_STR.at(mesh)));
  }
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

  #ifdef XDG_ENABLE_CUBQL
  if (rt == xdg::RTLibrary::CUBQL)
    return std::make_shared<xdg::CuBQLRayTracer>();
  #endif

  return nullptr;
}
