#include <array>
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

  const std::size_t batch_sizes[] = {1, 1024};
  for (std::size_t num_rays : batch_sizes) {
    DYNAMIC_SECTION("N=" << num_rays)
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

TEST_CASE("XDG batch ray fire multi-volume", "[rayfire][batch][cubql]")
{
  check_ray_tracer_supported(RTLibrary::CUBQL);

  auto xdg = XDG::create(MeshLibrary::MOAB, RTLibrary::CUBQL);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("pwr_pincell.h5m");
  mesh_manager->init();
  mesh_manager->parse_metadata();
  xdg->prepare_raytracer();

  struct VolumeCase {
    MeshID volume;
    Position origin;
  };

  const std::array<VolumeCase, 3> volume_cases {{
    {1, {0.0, 0.01, 0.1}},
    {2, {0.42, 0.01, 0.1}},
    {3, {0.50, 0.01, 0.1}}
  }};

  for (const auto& volume_case : volume_cases) {
    CAPTURE(volume_case.volume, volume_case.origin);
    REQUIRE(xdg->point_in_volume(volume_case.volume, volume_case.origin));
  }

  const std::array<Direction, 6> axis_aligned_directions {{
    {1.0, 0.0, 0.0},
    {-1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, -1.0, 0.0},
    {0.0, 0.0, 1.0},
    {0.0, 0.0, -1.0}
  }};

  // Build host-side ray-hit buffer and copy to device to handle every ray in one batch
  std::vector<XDGRayHit> host_ray_hits;
  host_ray_hits.reserve(volume_cases.size() * axis_aligned_directions.size());

  for (const auto& direction : axis_aligned_directions) {
    for (const auto& volume_case : volume_cases) {
      XDGRayHit ray_hit {};
      ray_hit.origin[0] = volume_case.origin.x;
      ray_hit.origin[1] = volume_case.origin.y;
      ray_hit.origin[2] = volume_case.origin.z;
      ray_hit.direction[0] = direction.x;
      ray_hit.direction[1] = direction.y;
      ray_hit.direction[2] = direction.z;
      ray_hit.t_min = 0.0;
      ray_hit.t_max = INFTY;
      ray_hit.volume = volume_case.volume;
      ray_hit.last_hit_primitive = ID_NONE;
      host_ray_hits.push_back(ray_hit);
    }
  }

  XDGRayHitBuffer ray_hits = xdg->allocate_ray_hits(host_ray_hits.size());
  const std::size_t buffer_size = host_ray_hits.size() * sizeof(XDGRayHit);
  REQUIRE(omp_target_memcpy(ray_hits.data,
                            host_ray_hits.data(),
                            buffer_size,
                            0,
                            0,
                            ray_hits.device_id,
                            omp_get_initial_device()) == 0);

  xdg->ray_fire_batch(ray_hits);

  REQUIRE(omp_target_memcpy(host_ray_hits.data(),
                            ray_hits.data,
                            buffer_size,
                            0,
                            0,
                            omp_get_initial_device(),
                            ray_hits.device_id) == 0);

  xdg->free_ray_hits(ray_hits);

  for (std::size_t ray_id = 0; ray_id < host_ray_hits.size(); ++ray_id) {
    const auto& batch_ray_hit = host_ray_hits[ray_id]; // recover rayhit for this ray from batch results

    const Position origin {batch_ray_hit.origin[0], batch_ray_hit.origin[1], batch_ray_hit.origin[2]};
    const Direction direction {batch_ray_hit.direction[0], batch_ray_hit.direction[1], batch_ray_hit.direction[2]};
    std::vector<MeshID> last_hit_prim;
    const auto scalar_hit = xdg->ray_fire(batch_ray_hit.volume, origin, direction, INFTY, HitOrientation::EXITING, &last_hit_prim);

    REQUIRE(scalar_hit.second != ID_NONE);
    REQUIRE(last_hit_prim.size() == 1);

    // Get expected values for this ray from scalar ray_fire and mesh manager for direct comparison against batch ray_fire results
    const MeshID expected_surface = scalar_hit.second;
    const double expected_distance = scalar_hit.first;
    const MeshID expected_primitive = last_hit_prim.back();
    const PointInVolume expected_point_in_volume = xdg->point_in_volume(batch_ray_hit.volume, origin, &direction) ? INSIDE : OUTSIDE;
    const MeshID expected_next_volume = mesh_manager->next_volume(batch_ray_hit.volume, expected_surface);
    const auto boundary_property = mesh_manager->get_surface_property(expected_surface, PropertyType::BOUNDARY_CONDITION);
    SurfaceBoundaryCondition expected_boundary_condition = UNSET;
    if (boundary_property.value == "transmission") {
      expected_boundary_condition = TRANSMISSION;
    } else if (boundary_property.value == "reflecting") {
      expected_boundary_condition = REFLECTIVE;
    }
    const auto vertices = mesh_manager->face_vertices(expected_primitive);
    const Direction expected_normal = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);

    INFO("ray index: " << ray_id);
    INFO("volume: " << batch_ray_hit.volume);
    INFO("origin: " << origin);
    INFO("direction: " << direction);
    INFO("batch ray hit: surface=" << batch_ray_hit.surface << ", distance=" << std::setprecision(17) << batch_ray_hit.distance);
    INFO("scalar hit: surface=" << expected_surface << ", distance=" << std::setprecision(17) << expected_distance);
    INFO("boundary property: " << boundary_property.value);
    
    REQUIRE(expected_boundary_condition != UNSET);
    REQUIRE(batch_ray_hit.surface == expected_surface);
    REQUIRE_THAT(batch_ray_hit.distance, Catch::Matchers::WithinAbs(expected_distance, tolerance));
    REQUIRE(batch_ray_hit.primitive == expected_primitive);
    REQUIRE(batch_ray_hit.point_in_volume == expected_point_in_volume);
    REQUIRE(batch_ray_hit.next_volume == expected_next_volume);
    REQUIRE(batch_ray_hit.boundary_condition == expected_boundary_condition);
    REQUIRE_THAT(batch_ray_hit.normal[0], Catch::Matchers::WithinAbs(expected_normal.x, tolerance));
    REQUIRE_THAT(batch_ray_hit.normal[1], Catch::Matchers::WithinAbs(expected_normal.y, tolerance));
    REQUIRE_THAT(batch_ray_hit.normal[2], Catch::Matchers::WithinAbs(expected_normal.z, tolerance));
  }
}
