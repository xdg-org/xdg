#include <random>

#include <catch2/catch_test_macros.hpp>

#include "xdg/constants.h"
#include "xdg/ray_tracers.h"
#include "xdg/mesh_managers.h"
#include "vulkan_probe.h"

static std::random_device rd;
static std::mt19937 gen(rd());

inline double rand_double(double min, double max)
{
  std::uniform_real_distribution<double> dis(min, max);
  return dis(gen);
}

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
}
