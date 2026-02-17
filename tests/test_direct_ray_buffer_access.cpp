// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/gprt/ray_tracer.h"
#include "xdg/xdg.h"
#include "mesh_mock.h"
#include "test_direct_ray_buffer_access_shared.h"
#include "util.h"
#include "gprt.h"

#include <chrono>

using namespace xdg;
using namespace xdg::test;

extern GPRTProgram test_direct_ray_buffer_access_deviceCode;

static RayPopulationCallback make_populate_callback(const std::vector<Position>& origins,
                                                    const std::vector<Direction>& directions,
                                                    const std::vector<MeshID>& volume_ids,
                                                    GPRTContext context,
                                                    GPRTComputeOf<ExternalRayParams> packRays) {
  return [&origins, &directions, volume_ids, context, packRays]
    (const DeviceRayHitBuffers& buffer, size_t num_rays) {

    // When passing arrays to the callback, ensure they are the correct size
    assert(origins.size() == num_rays);
    assert(directions.size() == num_rays); 

    // Convert to double3 for use on GPU
    std::vector<double3> origins_device(num_rays);
    std::vector<double3> directions_device(num_rays);
    for (size_t i = 0; i < num_rays; ++i) {
      origins_device[i] = {origins[i].x, origins[i].y, origins[i].z};
      directions_device[i] = {directions[i].x, directions[i].y, directions[i].z};
    }

    auto origins_buffer = gprtDeviceBufferCreate<double3>(context, num_rays, origins_device.data());
    auto directions_buffer = gprtDeviceBufferCreate<double3>(context, num_rays, directions_device.data());
    auto volume_ids_buffer = gprtDeviceBufferCreate<int32_t>(context, num_rays, volume_ids.data()); 

    constexpr uint32_t threads_per_group = 256;
    const uint32_t groups = static_cast<uint32_t>((num_rays + threads_per_group - 1) / threads_per_group);

    ExternalRayParams params = {};
    params.xdgRays = static_cast<dblRay*>(buffer.rayDevPtr);
    params.origins = gprtBufferGetDevicePointer(origins_buffer);
    params.directions = gprtBufferGetDevicePointer(directions_buffer);
    params.num_rays = static_cast<uint32_t>(num_rays);
    params.total_threads = groups * threads_per_group;
    params.volume_mesh_ids = gprtBufferGetDevicePointer(volume_ids_buffer); // Pass array of volume IDs to compute shader
    params.enabled = 1u;

    gprtComputeLaunch(packRays,
                      { groups, 1, 1 },
                      { threads_per_group, 1, 1 },
                      params);
    gprtComputeSynchronize(context);

    gprtBufferDestroy(origins_buffer);
    gprtBufferDestroy(directions_buffer);
    if (volume_ids_buffer) {
      gprtBufferDestroy(volume_ids_buffer);
    }
  };
}

// This is a GPU only test - skip if no GPU ray tracing backends are enabled
TEMPLATE_TEST_CASE("Ray Fire with external populated rays", "[rayfire][mock]",
                   GPRT_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel.h5m");
    mesh_manager->init();
    xdg->prepare_raytracer();

    std::vector<Position> origins;
    std::vector<Direction> directions;
    size_t N = 64;
    make_rays(N, origins, directions);

    auto gprt_rt = std::dynamic_pointer_cast<GPRTRayTracer>(xdg->ray_tracing_interface());
    REQUIRE(gprt_rt);

    std::vector<MeshID> volumes = mesh_manager->volumes();
    REQUIRE(volumes.size() >= 2);
    const MeshID volume_id = volumes[0];
    const MeshID volume_id_alt = volumes[1];
    GPRTContext context = gprt_rt->context();
    GPRTModule module = gprtModuleCreate(context, test_direct_ray_buffer_access_deviceCode);
    auto packRays = gprtComputeCreate<ExternalRayParams>(context, module, "pack_external_rays");

    std::vector<double> expected_distances(N, INFTY);
    std::vector<MeshID> expected_surfaces(N, ID_NONE);
    std::vector<MeshID> volume_ids(N, volume_id);
    for (size_t i = 0; i < N; ++i) {
      volume_ids[i] = (i % 2 == 0) ? volume_id : volume_id_alt; // Volume IDs alternating between two volumes
      const auto [dist, surf] = xdg->ray_fire(volume_ids[i], origins[i], directions[i]);
      expected_distances[i] = dist;
      expected_surfaces[i] = surf;
    }

    // Create callback to populate rays on device
    RayPopulationCallback populate_callback = make_populate_callback(origins,
                                                                     directions,
                                                                     volume_ids,
                                                                     context,
                                                                     packRays);
      
    // Populate rays via external API
    xdg->populate_rays_external(N, populate_callback);

    xdg->ray_fire_prepared(N);
    std::vector<dblHit> hits;
    gprt_rt->transfer_hits_buffer_to_host(N, hits);

    REQUIRE(hits.size() == N);
    for (size_t i = 0; i < N; ++i) {
      REQUIRE(hits[i].surf_id == expected_surfaces[i]);
      if (expected_surfaces[i] != ID_NONE) {
        REQUIRE_THAT(hits[i].distance, Catch::Matchers::WithinAbs(expected_distances[i], 1e-6));
      }
    }

    gprtComputeDestroy(packRays);
    gprtModuleDestroy(module);
  }
}

TEMPLATE_TEST_CASE("Point-in-volume with external populated rays", "[piv][mock]",
                   GPRT_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel.h5m");
    mesh_manager->init();
    xdg->prepare_raytracer();

    std::vector<Position> points;
    std::vector<Direction> directions;
    size_t N = 64;
    make_points(N, points, directions);

    auto gprt_rt = std::dynamic_pointer_cast<GPRTRayTracer>(xdg->ray_tracing_interface());
    REQUIRE(gprt_rt);

    std::vector<MeshID> volumes = mesh_manager->volumes();
    REQUIRE(volumes.size() >= 2);
    const MeshID volume_id = volumes[0];
    const MeshID volume_id_alt = volumes[1];
    GPRTContext context = gprt_rt->context();
    GPRTModule module = gprtModuleCreate(context, test_direct_ray_buffer_access_deviceCode);
    auto packRays = gprtComputeCreate<ExternalRayParams>(context, module, "pack_external_rays");

    std::vector<uint8_t> expected_piv(N, 0);
    std::vector<MeshID> volume_ids(N, volume_id);
    for (size_t i = 0; i < N; ++i) {
      volume_ids[i] = (i % 2 == 0) ? volume_id : volume_id_alt;
      expected_piv[i] = static_cast<uint8_t>(xdg->point_in_volume(volume_ids[i], points[i], &directions[i]));
    }

    RayPopulationCallback populate_callback = make_populate_callback(points,
                                                                      directions,
                                                                      volume_ids,
                                                                      context,
                                                                      packRays);
    xdg->populate_rays_external(N, populate_callback);

    xdg->point_in_volume_prepared(N);
    std::vector<dblHit> hits;
    gprt_rt->transfer_hits_buffer_to_host(N, hits);

    REQUIRE(hits.size() == N);
    for (size_t i = 0; i < N; ++i) {
      const auto expected = expected_piv[i] ? xdg::PointInVolume::INSIDE : xdg::PointInVolume::OUTSIDE; // convert back to enum
      REQUIRE(hits[i].piv == expected);
    }

    gprtComputeDestroy(packRays);
    gprtModuleDestroy(module);
  }
}
