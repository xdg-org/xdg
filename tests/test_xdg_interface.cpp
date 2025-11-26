#include <memory>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/mesh_managers.h"
#include "util.h"

using namespace xdg;

TEST_CASE("XDG Interface") {
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>();
  REQUIRE(xdg->ray_tracing_interface() == nullptr);
  REQUIRE(xdg->mesh_manager() == nullptr);
}

TEST_CASE("XDG Factory Creation") {
  // Create xdg instances using the factory method

  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);
  auto mesh_backend = GENERATE(MeshLibrary::MOAB, MeshLibrary::LIBMESH);

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

TEST_CASE("XDG Constructor") {

  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);
  auto mesh_backend = GENERATE(MeshLibrary::MOAB, MeshLibrary::LIBMESH);

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
