// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "mesh_mock.h"
#include "util.h"

#include <chrono>

using namespace xdg;

// ------- single test, multiple sections (one per built backend) --------------

TEST_CASE("Ray Fire on MeshMock (per-backend sections) edge cases", "[rayfire][mock]") {
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
    Position origin {0.0, 0.0, 0.0};
    Direction direction {1.0, 0.0, 0.0};
    std::pair<double, MeshID> intersection;

    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    direction *= -1;
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

    direction = {0.0, 1.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(6.0, 1e-6));
    direction *= -1;
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(3.0, 1e-6));

    direction = {0.0, 0.0, 1.0};
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(7.0, 1e-6));
    direction *= -1;
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(4.0, 1e-6));

    // fire from the outside of the cube toward each face, ensuring that the intersection distances are correct
    // rays should skip entering intersections and intersect with the far side of the cube
    origin = {-10.0, 0.0, 0.0};
    direction = {1.0, 0.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(15.0, 1e-6));

    origin = {10.0, 0.0, 0.0};
    direction = {-1.0, 0.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(12.0, 1e-6));

    // fire from the outside of the cube toward each face, ensuring that the intersection distances are correct
    // in this case rays are fired with a HitOrientation::ENTERING. Rays should hit the first surface intersected
    origin = {-10.0, 0.0, 0.0};
    direction = {1.0, 0.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::ENTERING);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(8.0, 1e-6));

    origin = {10.0, 0.0, 0.0};
    direction = {-1.0, 0.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::ENTERING);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    // limit distance of the ray, shouldn't get a hit
    origin = {0.0, 0.0, 0.0};
    direction = {1.0, 0.0, 0.0};
    intersection = rti->ray_fire(volume_tree, origin, direction, 4.5);
    REQUIRE(intersection.second == ID_NONE);

    // if the distance is just enough, we should still get a hit
    intersection = rti->ray_fire(volume_tree, origin, direction, 5.1);
    REQUIRE(intersection.second != ID_NONE);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    // Test excluding primitives, fire a ray from the origin and log the hit face
    // By providing the hit face as an excluded primitive in a subsequent ray fire,
    // there should be no intersection returned
    std::vector<MeshID> exclude_primitives;
    intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::EXITING, &exclude_primitives);
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));
    REQUIRE(exclude_primitives.size() == 1);

    intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::EXITING, &exclude_primitives);
    REQUIRE(intersection.second == ID_NONE);
  }
}

TEST_CASE("Batch API Ray Fire on MeshMock", "[rayfire][mock][batch]") {
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend);
    if (rt_backend == RTLibrary::EMBREE) {
      SKIP("Skipping batch query mechanics test for Embree: batch API not implemented.");
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

    // Helper to synthesize origins/directions for N rays
    auto make_rays = [](size_t N, std::vector<Position>& origins, std::vector<Direction>& directions) {
      origins.clear(); 
      directions.clear();
      origins.reserve(N); 
      directions.reserve(N);
      for (size_t i = 0; i < N; ++i) {
        int axis = int(i % 3);
        double s = (i % 2) ? 1.0 : -1.0;
        origins.push_back({0.0, 0.0, 0.0});
        if (axis == 0) directions.push_back({s, 0.0, 0.0});
        else if (axis == 1) directions.push_back({0.0, s, 0.0});
        else directions.push_back({0.0, 0.0, s});
      }
    };

    std::vector<Position> origins;
    std::vector<Direction> directions;
    size_t N; 

    // ---- N = 0 ----
    SECTION("N=0 no-op") {
      rti->ray_fire(volume_tree, nullptr, nullptr, 0, nullptr, nullptr,
                    INFTY, HitOrientation::EXITING, nullptr);
      SUCCEED("N=0 completed without error");
    }

    // ---- N = 1 ----
    SECTION("N=1 equals scalar") {
      N = 1;
      make_rays(N, origins, directions);

      auto [dist_scalar, id_scalar] = rti->ray_fire(volume_tree, origins[0], directions[0], INFTY, HitOrientation::EXITING);

      double dist_batch = -1.0;
      MeshID id_batch = ID_NONE;
      rti->ray_fire(volume_tree, origins.data(), directions.data(), 1,
                    &dist_batch, &id_batch, INFTY, HitOrientation::EXITING, nullptr);

      REQUIRE(id_batch == id_scalar);
      REQUIRE_THAT(dist_batch, Catch::Matchers::WithinAbs(dist_scalar, 1e-6));
    }

    // ---- N = 64 ----
    SECTION("N=64 matches scalar for all") {
      N = 64;
      make_rays(N, origins, directions);

      std::vector<double> dist_scalar(64, INFTY);
      std::vector<MeshID> id_scalar(64, ID_NONE);
      for (size_t i = 0; i < 64; ++i) {
        auto [d, id] = rti->ray_fire(volume_tree, origins[i], directions[i], INFTY, HitOrientation::EXITING);
        dist_scalar[i] = d; id_scalar[i] = id;
      }

      std::vector<double> dist_batch(64, -1.0);
      std::vector<MeshID> id_batch(64, ID_NONE);
      rti->ray_fire(volume_tree, origins.data(), directions.data(), origins.size(),
                    dist_batch.data(), id_batch.data(), INFTY, HitOrientation::EXITING, nullptr);

      for (size_t i = 0; i < 64; ++i) {
        REQUIRE(id_batch[i] == id_scalar[i]);
        REQUIRE_THAT(dist_batch[i], Catch::Matchers::WithinAbs(dist_scalar[i], 1e-6));
      }
    }

    // ---- N = 100,000 ----
    SECTION("N=100,00 batch with basic sanity checks") {
      N = 100000;
      make_rays(N, origins, directions);

      // Batch compute
      std::vector<double> dist_batch(N, -1.0);
      std::vector<MeshID> id_batch(N, ID_NONE);
      BENCHMARK("Batch ray fire with N = 100,000")
      {
       return rti->ray_fire(volume_tree, origins.data(), directions.data(), N,
                            dist_batch.data(), id_batch.data(), INFTY, HitOrientation::EXITING, nullptr);
      };
      // Basic sanity checks over 100 rays
      for (size_t i = 0; i < N; i += N/100) {
        REQUIRE(id_batch[i] != ID_NONE);
        REQUIRE(std::isfinite(dist_batch[i]));
        REQUIRE(dist_batch[i] >= 0.0);
      }
    }
  }
}
