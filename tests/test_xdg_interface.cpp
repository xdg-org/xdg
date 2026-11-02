#include <cmath>
#include <memory>
#include <utility>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/mesh_managers.h"
#include "xdg/ray_tracers.h"
#include "xdg/xdg.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test;

TEST_CASE("XDG Interface") {
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>();
  REQUIRE(xdg->ray_tracing_interface() == nullptr);
  REQUIRE(xdg->mesh_manager() == nullptr);
}

TEMPLATE_LIST_TEST_CASE("XDG Factory Creation", "[xdg][factory]", XDGBackends) {
  using MeshTag = typename TestType::first_type;
  using RayTag = typename TestType::second_type;
  constexpr auto mesh_backend = MeshTag::value;
  constexpr auto rt_backend = RayTag::value;

  DYNAMIC_SECTION(fmt::format("Mesh Backend = {}, RT Backend = {}", mesh_backend, rt_backend)) {
    check_mesh_library_supported(mesh_backend); // skip if mesh backend not enabled at configuration time
    check_ray_tracer_supported(rt_backend);     // skip if rt backend not enabled at configuration

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend, rt_backend);

    // Check that the factory method creates interface pointers
    REQUIRE(xdg->ray_tracing_interface() != nullptr);
    REQUIRE(xdg->mesh_manager() != nullptr);

    // Check that the factory method creates RT interface pointers of the right types
    switch (rt_backend) {
      #ifdef XDG_ENABLE_EMBREE
      case RTLibrary::EMBREE:
        REQUIRE(std::dynamic_pointer_cast<EmbreeRayTracer>(xdg->ray_tracing_interface()) != nullptr);
        break;
      #endif

      #ifdef XDG_ENABLE_GPRT
      case RTLibrary::GPRT:
        REQUIRE(std::dynamic_pointer_cast<GPRTRayTracer>(xdg->ray_tracing_interface()) != nullptr);
        break;
      #endif
    }

    // Check that the factory method creates MeshManager interface pointers of the right types
    switch (mesh_backend) {
      #ifdef XDG_ENABLE_MOAB
      case MeshLibrary::MOAB:
        REQUIRE(std::dynamic_pointer_cast<MOABMeshManager>(xdg->mesh_manager()) != nullptr);
        break;
      #endif

      #ifdef XDG_ENABLE_LIBMESH
      case MeshLibrary::LIBMESH:
        REQUIRE(std::dynamic_pointer_cast<LibMeshManager>(xdg->mesh_manager()) != nullptr);
        break;
      #endif
    }
  }
}

TEMPLATE_LIST_TEST_CASE("XDG Constructor", "[xdg][constructor]", XDGBackends) {
  using MeshTag = typename TestType::first_type;
  using RayTag = typename TestType::second_type;
  constexpr auto mesh_backend = MeshTag::value;
  constexpr auto rt_backend = RayTag::value;

  DYNAMIC_SECTION(fmt::format("Mesh Backend = {}, RT Backend = {}", mesh_backend, rt_backend)) {
    check_mesh_library_supported(mesh_backend); // skip if mesh backend not enabled at configuration time
    check_ray_tracer_supported(rt_backend);     // skip if rt backend not enabled at configuration

    std::shared_ptr<MeshManager> mm = create_mesh_manager(mesh_backend);
    std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm, rt_backend);

    // Check that constructor creates RT interface pointers of the right types
    switch (rt_backend) {
      #ifdef XDG_ENABLE_EMBREE
      case RTLibrary::EMBREE:
        REQUIRE(std::dynamic_pointer_cast<EmbreeRayTracer>(xdg->ray_tracing_interface()) != nullptr);
        break;
      #endif

      #ifdef XDG_ENABLE_GPRT
      case RTLibrary::GPRT:
        REQUIRE(std::dynamic_pointer_cast<GPRTRayTracer>(xdg->ray_tracing_interface()) != nullptr);
        break;
      #endif
    }

    // Check that the constructor creates MeshManager interface pointers of the right types
    switch (mesh_backend) {
      #ifdef XDG_ENABLE_MOAB
      case MeshLibrary::MOAB:
        REQUIRE(std::dynamic_pointer_cast<MOABMeshManager>(xdg->mesh_manager()) != nullptr);
        break;
      #endif

      #ifdef XDG_ENABLE_LIBMESH
      case MeshLibrary::LIBMESH:
        REQUIRE(std::dynamic_pointer_cast<LibMeshManager>(xdg->mesh_manager()) != nullptr);
        break;
      #endif
    }
  }
}

TEMPLATE_LIST_TEST_CASE("XDG Query API", "[xdg][queries][rayfire][point_in_volume]",
                        XDGBackends) {
  using MeshTag = typename TestType::first_type;
  using RayTag = typename TestType::second_type;
  constexpr auto mesh_backend = MeshTag::value;
  constexpr auto rt_backend = RayTag::value;

  DYNAMIC_SECTION(fmt::format("Mesh Backend = {}, RT Backend = {}", mesh_backend, rt_backend)) {
    check_mesh_library_supported(mesh_backend); // skip if mesh backend not enabled at configuration time
    check_ray_tracer_supported(rt_backend);     // skip if rt backend not enabled at configuration

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend, rt_backend);
    REQUIRE(xdg != nullptr);

    const char* file = mesh_backend == MeshLibrary::MOAB ? "cube.h5m" : "brick.exo";
    const auto& mm = xdg->mesh_manager();
    mm->load_file(file);
    mm->init();
    xdg->prepare_raytracer();

    constexpr MeshID volume = 1;
    const Position origin {0.0, 0.0, 0.0};
    const Direction direction {0.0, 0.0, 1.0};

    // Point [0, 0, 0] is in volume 1.
    REQUIRE(xdg->point_in_volume(volume, origin));

    // We should hit surface 1 at a distance of 5.0
    const auto hit = xdg->ray_fire(volume, origin, direction);
    REQUIRE(hit.second == 1);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(5.0, 1e-6));
  }
}
