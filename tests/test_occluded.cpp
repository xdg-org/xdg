// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/mesh_manager_interface.h"
#include "xdg/xdg.h"

#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Test Occluded")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing, just good practice to call it
   // Generate one test run per enabled backend
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);

    auto mm = std::make_shared<MeshMock>(false);
    mm->init();
    REQUIRE(mm->mesh_library() == MeshLibrary::MOCK);

    auto [volume_tree, element_tree] = rti->register_volume(mm, mm->volumes()[0]);
    REQUIRE(volume_tree != ID_NONE);
    REQUIRE(element_tree == ID_NONE);

    rti->init(); // Ensure ray tracer is initialized (e.g. build SBT for GPRT)

    // setup ray to fire that won't hit the mock model
    Position r {-100.0, 0.0, 0.0};
    Direction u {1.0, 0.0, 0.0};
    double dist {0.0};

    bool result = rti->occluded(volume_tree, r, u, dist);
    REQUIRE(result == true);

    u = {-1.0, 0.0, 0.0};
    result = rti->occluded(volume_tree, r, u, dist);
    REQUIRE(result == false);
  }
}