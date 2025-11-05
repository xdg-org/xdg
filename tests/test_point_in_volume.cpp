// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>


// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

// ---------- single test, sections per backend --------------------------------

TEST_CASE("Point-in-volume on MeshMock edge cases", "[piv][mock]") 
{
  // Generate one test run per enabled backend
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);
    rti->init();

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

TEST_CASE("Batch API Point-in-volume on MeshMock", "[piv][mock][batch]") {
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend);
    if (rt_backend == RTLibrary::EMBREE) {
      SKIP("Skipping PIV batch for Embree: batch API not implemented yet");
    }

    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);

    auto mm = std::make_shared<MeshMock>(false);
    mm->init();
    REQUIRE(mm->mesh_library() == MeshLibrary::MOCK);

    auto [volume_tree, element_tree] = rti->register_volume(mm, mm->volumes()[0]);
    REQUIRE(volume_tree != ID_NONE);
    REQUIRE(element_tree == ID_NONE);

    rti->init();

    // Build 64 points: alternate inside/outside with some null directions
    const size_t num_points = 64;
    std::vector<Position> points(num_points);
    std::vector<Direction> directions(num_points); // contiguous directions (ignored if has_dir[i]==0)
    std::vector<uint8_t> has_dir(num_points, 0); // mask: 1 => use directions[i], 0 => use default

    for (int i = 0; i < num_points; ++i) {
      // even i: origin (inside); odd i: just outside +X
      points[i] = (i % 2 == 0) ? Position{0,0,0} : Position{5.1,0,0};

      // every 3rd ray has no direction has_dir == 0; others alternate ±X with has_dir == 1
      if (i % 3 != 0) {
        directions[i] = (i % 2 == 0) ? Direction{1,0,0} : Direction{-1,0,0};
        has_dir[i] = 1;
      } else {
        has_dir[i] = 0; // mask out direction and use default 
      }
    }

    // Store results of scalar point_in_volume calls to verify batch against scalar
    std::vector<uint8_t> truth(num_points, 0);
    for (size_t i = 0; i < num_points; ++i) {
      const Direction* dptr = has_dir[i] ? &directions[i] : nullptr;
      truth[i] = static_cast<uint8_t>(rti->point_in_volume(volume_tree, points[i], dptr));
    }

    SECTION("N=0 no-op") {
      rti->batch_point_in_volume(volume_tree, nullptr, nullptr, 0, nullptr, nullptr);
      SUCCEED("N=0 completed without error");
    }

    SECTION("N=1") {
      uint8_t result = 0xFF; // sentinel
      rti->batch_point_in_volume(volume_tree, &points[0], &directions[0], 1, &result, nullptr);
      REQUIRE((result == 0 || result == 1));
      REQUIRE(result == truth[0]);
    }

    SECTION("N=64") {
      std::vector<uint8_t> results(num_points, 0xFF);
      rti->batch_point_in_volume(volume_tree, points.data(), directions.data(), num_points, results.data(), nullptr);
      for (size_t i = 0; i < points.size(); ++i) {
        REQUIRE(results[i] == truth[i]);
      }
    }
  }
}
