// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

static void make_points(size_t N,
                  std::vector<Position>& points,
                  std::vector<Direction>& directions,
                  std::vector<uint8_t>& has_dir) 
{
  points.resize(N);
  directions.resize(N);
  has_dir.assign(N, 0);
  for (size_t i = 0; i < N; ++i) {
    // evens inside (origin), odds just outside +X
    points[i] = (i % 2 == 0) ? xdg::Position{0,0,0} : xdg::Position{5.1,0,0};
    // every 3rd masked off; others alternate ±X
    if (i % 3 != 0) {
      directions[i] = (i % 2 == 0) ? xdg::Direction{ 1,0,0} : xdg::Direction{-1,0,0};
      has_dir[i] = 1;
    }
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
      rti->point_in_volume(volume_tree, nullptr, nullptr, 0, nullptr, nullptr);
      SUCCEED("N=0 completed without error");
    }

    SECTION("N=1") {
      N = 1;
      make_points(N, points, directions, has_dir);

      auto scalar_result = rti->point_in_volume(volume_tree, points[0], &directions[0]);

      std::vector<uint8_t> batch_result(N, 0xFF);
      rti->point_in_volume(volume_tree, &points[0], &directions[0], N, &batch_result[0], nullptr);
      REQUIRE(batch_result[0] == static_cast<uint8_t>(scalar_result));
    }

    SECTION("N=64") {
      N = 64;
      make_points(N, points, directions, has_dir);

      // Store results of scalar point_in_volume calls to verify batch against scalar
      std::vector<uint8_t> scalar_results(N, 0);
      for (size_t i = 0; i < N; ++i) {
        const Direction* dptr = has_dir[i] ? &directions[i] : nullptr;
        scalar_results[i] = static_cast<uint8_t>(rti->point_in_volume(volume_tree, points[i], dptr));
      }

      std::vector<uint8_t> batch_results(N, 0xFF);
      rti->point_in_volume(volume_tree, points.data(), directions.data(), N, batch_results.data(), has_dir.data());
      for (size_t i = 0; i < points.size(); ++i) {
        REQUIRE(batch_results[i] == scalar_results[i]);
      }
    }

    // ---- N = 100,000 ----
    SECTION("N=100,00 batch with basic sanity checks") {
      N = 100000;
      make_points(N, points, directions, has_dir);

      std::vector<uint8_t> batch_results(N, 0xFF);
      BENCHMARK("Batch point_in_volume with N = 100,000")
      {
       return rti->point_in_volume(volume_tree, points.data(), directions.data(), N, batch_results.data(), has_dir.data());
      };
    }
  }
}