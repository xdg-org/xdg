// for testing
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>


// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "util.h"
#include "mesh_mock.h"

using namespace xdg;
using namespace xdg::test;

// ---------- single test, sections per backend --------------------------------

TEMPLATE_TEST_CASE("Point-in-volume on MeshMock", "[piv][mock]",
                   Embree_Raytracer,
                   GPRT_Raytracer) 
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);

    // Keep MeshMock usage consistent across backends
    auto mm = std::make_shared<MeshMock>(false);
    mm->init();
    REQUIRE(mm->mesh_library() == MeshLibrary::MOCK);

    auto [volume_tree, element_tree] = rti->register_volume(mm, mm->volumes()[0]);
    REQUIRE(volume_tree != ID_NONE);
    REQUIRE(element_tree == ID_NONE);

    rti->init(); // Ensure ray tracer is initialized (e.g. build SBT for GPRT)

    Position point {0.0, 0.0, 0.0};
    bool result = rti->point_in_volume(volume_tree, point);
    REQUIRE(result == true);

    point = {0.0, 0.0, 1000.0};
    result = rti->point_in_volume(volume_tree, point);
    REQUIRE(result == false);

    // test a point just inside the positive x boundary
    point = {4.0 - 1e-6, 0.0, 0.0};
    result = rti->point_in_volume(volume_tree, point);
    REQUIRE(result == true);

    // test a point just outside on the positive x boundary
    // no direction
    point = {5.001, 0.0, 0.0};
    result = rti->point_in_volume(volume_tree, point);
    REQUIRE(result == false);

    // test a point on the positive x boundary
    // and provide a direction
    point = {5.0, 0.0, 0.0};
    Direction dir {1.0, 0.0, 0.0};
    result = rti->point_in_volume(volume_tree, point, &dir);
    REQUIRE(result == true);

    // test a point just outside the positive x boundary
    // and provide a direction
    point = {5.1, 0.0, 0.0};
    dir = {1.0, 0.0, 0.0};
    result = rti->point_in_volume(volume_tree, point, &dir);
    REQUIRE(result == false);

    // test a point just outside the positive x boundary,
    // flip the direction
    point = {5.1, 0.0, 0.0};
    dir = {-1.0, 0.0, 0.0};
    result = rti->point_in_volume(volume_tree, point, &dir);
    REQUIRE(result == false);
  }
}