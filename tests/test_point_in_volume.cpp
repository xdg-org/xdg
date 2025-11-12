// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

static void make_points(size_t N,
                        std::vector<Position>& points,
                        std::vector<Direction>& directions) 
{
  points.resize(N);
  directions.resize(N);
  for (size_t i = 0; i < N; ++i) {
    // evens inside (origin), odds just outside +X
    points[i] = (i % 2 == 0) ? xdg::Position{0,0,0} : xdg::Position{5.1,0,0};
    // alternate ±X directions
    directions[i] = (i % 2 == 0) ? xdg::Direction{ 1,0,0}
                                 : xdg::Direction{-1,0,0};
  }
}

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

    std::vector<Position> points;
    std::vector<Direction> directions;
    std::vector<uint8_t> has_dir;
    size_t N; 

    SECTION("N=0 no-op") {
      rti->point_in_volume(volume_tree, 
                           nullptr, /*points*/
                           0,       /*num_points*/
                           nullptr /*results*/);
      SUCCEED("N=0 completed without error");
    }

    SECTION("N=1") {
      N = 1;
      make_points(N, points, directions);

      auto scalar_result = static_cast<uint8_t>(rti->point_in_volume(volume_tree, points[0], &directions[0]));

      std::vector<uint8_t> batch_result(N, 0xFF);
      rti->point_in_volume(volume_tree, points.data(), N, batch_result.data(), directions.data());
      REQUIRE(batch_result[0] == scalar_result);
    }

    SECTION("N=64") {
      N = 64;
      make_points(N, points, directions);

      // Store results of scalar point_in_volume calls to verify batch against scalar
      std::vector<uint8_t> scalar_results(N, 0);
      for (size_t i = 0; i < N; ++i) {
        scalar_results[i] = static_cast<uint8_t>(rti->point_in_volume(volume_tree, points[i], &directions[i]));
      }

      std::vector<uint8_t> batch_results(N, 0xFF);
      rti->point_in_volume(volume_tree, points.data(), N, batch_results.data(), directions.data());
      for (size_t i = 0; i < points.size(); ++i) {
        REQUIRE(batch_results[i] == scalar_results[i]);
      }
    }
  }
}