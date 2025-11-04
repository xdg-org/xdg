// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>


// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "mesh_mock.h"
#include "util.h"

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

    // Create a set of 64 rays to be used throughout this test_case
    std::vector<Position> origins;
    std::vector<Direction> directions;
    origins.reserve(64); 
    directions.reserve(64);
    for (int i = 0; i < 64; ++i) {
      int axis = i % 3; 
      double s = (i % 2) ? 1.0 : -1.0;
      origins.push_back({0,0,0});
      directions.push_back(axis == 0 ? Direction{s,0,0}
                           : axis == 1 ? Direction{0,s,0}
                                       : Direction{0,0,s});
    }
    
    // Store results of scalar ray_fires to verify batch against scalar
    std::vector<double> scalar_ray_fire_distances(origins.size(), INFTY);
    std::vector<MeshID> scalar_ray_fire_surface_id(origins.size(), ID_NONE);
    for (size_t i = 0; i < origins.size(); ++i) {
      auto [distance, surfID] = rti->ray_fire(volume_tree, origins[i], directions[i], INFTY, HitOrientation::EXITING);
      scalar_ray_fire_distances[i] = distance;
      scalar_ray_fire_surface_id[i] = surfID;
    }

    SECTION("N=0 no-op") {
      rti->batch_ray_fire(volume_tree, nullptr, nullptr, 0, nullptr, nullptr,
                          INFTY, HitOrientation::EXITING, nullptr);
      SUCCEED("N=0 completed without error");
    }

    SECTION("N=1 equals scalar") {
      double hd;
      MeshID sid = ID_NONE;

      rti->batch_ray_fire(volume_tree, &origins[0], &directions[0], 1, &hd, 
                          &sid, INFTY, HitOrientation::EXITING, nullptr);

      REQUIRE(sid != ID_NONE); // expect a hit 
      
      // Ensure that hit matches scalar ray_fire
      REQUIRE_THAT(hd, Catch::Matchers::WithinAbs(scalar_ray_fire_distances[0], 1e-6));
      REQUIRE(sid == scalar_ray_fire_surface_id[0]);
    }

    SECTION("N=64") {
      std::vector<double> hd(origins.size(), -1.0);
      std::vector<MeshID> sid(origins.size(), ID_NONE);
      rti->batch_ray_fire(volume_tree, origins.data(), directions.data(),
                          origins.size(), hd.data(), sid.data(),
                          INFTY, HitOrientation::EXITING, nullptr);
      
      // Ensure that hits match scalar ray_fires
      for (size_t i = 0; i < origins.size(); ++i) {
        REQUIRE_THAT(hd[i], Catch::Matchers::WithinAbs(scalar_ray_fire_distances[i], 1e-6));
        REQUIRE(sid[i] == scalar_ray_fire_surface_id[i]);
      }
    }
  }
}
