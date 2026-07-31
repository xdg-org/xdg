#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <omp.h>

#include "mesh_mock.h"
#include "util.h"
#include "xdg/constants.h"
#include "xdg/device_ray.h"
#include "xdg/util/rng.h"
#include "xdg/xdg.h"

using namespace xdg;
using namespace xdg::test;

constexpr double tolerance = 1e-6;

TEST_CASE("XDG batch ray-hit buffer lifecycle", "[batch][cubql]")
{
  check_ray_tracer_supported(RTLibrary::CUBQL);

  auto mesh_manager = std::make_shared<MeshMock>(false);
  auto xdg = std::make_shared<XDG>(mesh_manager, RTLibrary::CUBQL);

  const XDGRayHitBuffer empty_buffer = xdg->allocate_ray_hits(0);
  REQUIRE(empty_buffer.data == nullptr);
  REQUIRE(empty_buffer.count == 0);
  REQUIRE(empty_buffer.device_id == -1);

  XDGRayHitBuffer ray_hits = xdg->allocate_ray_hits(64);
  REQUIRE(ray_hits.data != nullptr);
  REQUIRE(ray_hits.count == 64);
  REQUIRE(ray_hits.device_id >= 0);
  xdg->free_ray_hits(ray_hits);
  REQUIRE(ray_hits.data == nullptr);
  REQUIRE(ray_hits.count == 0);
  REQUIRE(ray_hits.device_id == -1);
}

TEST_CASE("XDG batch ray fire accepts an empty buffer",
          "[rayfire][batch][cubql]")
{
  check_ray_tracer_supported(RTLibrary::CUBQL);

  auto mesh_manager = std::make_shared<MeshMock>(false);
  auto xdg = std::make_shared<XDG>(mesh_manager, RTLibrary::CUBQL);

  REQUIRE_NOTHROW(xdg->ray_fire_batch({}));
}

// The batch ray-fire API is currently implemented only by cuBQL.
// This test also populates its input buffer using OpenMP target offload,
// so support for another backend may require a different population path.
TEST_CASE("XDG batch ray fire matches scalar queries on MeshMock",
          "[rayfire][batch][cubql]")
{
  check_ray_tracer_supported(RTLibrary::CUBQL);

  auto mesh_manager = std::make_shared<MeshMock>(false);
  mesh_manager->init();

  auto xdg = std::make_shared<XDG>(mesh_manager, RTLibrary::CUBQL);
  xdg->prepare_raytracer();

  const std::size_t batch_sizes[] = {1, 64};
  for (std::size_t num_rays : batch_sizes) {
    DYNAMIC_SECTION("N=" << num_rays << " matches scalar")
    {
      XDGRayHitBuffer ray_hits = xdg->allocate_ray_hits(num_rays);

      REQUIRE(ray_hits.data != nullptr);

      XDGRayHit* d_ray_hits = ray_hits.data;
      const int device_id = ray_hits.device_id;
      const std::uint32_t seed = 12345u;
      const MeshID volume = mesh_manager->volumes()[0];

      #pragma omp target teams distribute parallel for device(device_id) \
        is_device_ptr(d_ray_hits)
      for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
        std::uint32_t state = seed ^ static_cast<std::uint32_t>(ray_id);

        XDGRayHit ray_hit {};
        ray_hit.origin[0] = 0.0;
        ray_hit.origin[1] = 0.0;
        ray_hit.origin[2] = 0.0;
        random_unit_direction_lcg(state, ray_hit.direction);
        ray_hit.t_min = 0.0;
        ray_hit.t_max = INFTY;
        ray_hit.volume = volume;
        ray_hit.last_hit_primitive = ID_NONE;
        d_ray_hits[ray_id] = ray_hit;
      }

      xdg->ray_fire_batch(ray_hits); // Perform device side ray tracing

      std::vector<XDGRayHit> batch_results(num_rays);
      const int copy_status = omp_target_memcpy(batch_results.data(),
                                                ray_hits.data,
                                                num_rays * sizeof(XDGRayHit),
                                                0,
                                                0,
                                                omp_get_initial_device(),
                                                ray_hits.device_id);
      REQUIRE(copy_status == 0); // Ensure omp memcpy was successful before checking results

      xdg->free_ray_hits(ray_hits);

      // Compare cuBQL batch results with cuBQL scalar ray_fire results
      // Note that cuBQL scalar ray_fire is cross-checked against embree in test_ray_tracer_cross_check.cpp
      const Position origin {0.0, 0.0, 0.0};
      for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
        std::uint32_t state = seed ^ static_cast<std::uint32_t>(ray_id);
        double direction_values[3];
        random_unit_direction_lcg(state, direction_values);
        const Direction direction {direction_values[0], direction_values[1], direction_values[2]};
        const auto scalar_result = xdg->ray_fire(volume, origin, direction);

        INFO("ray index: " << ray_id);
        INFO("direction: " << direction);
        INFO("batch result: surface=" << batch_results[ray_id].surface << ", distance=" << std::setprecision(17) << batch_results[ray_id].distance);
        INFO("scalar result: surface=" << scalar_result.second << ", distance=" << std::setprecision(17)  << scalar_result.first);
        REQUIRE(batch_results[ray_id].surface == scalar_result.second);
        REQUIRE_THAT(batch_results[ray_id].distance, Catch::Matchers::WithinAbs(scalar_result.first, tolerance));
      }
    }
  }
}
