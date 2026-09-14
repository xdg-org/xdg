#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "argparse/argparse.hpp"
#include <fmt/ranges.h>

#include "xdg/config.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/timer.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "ray_benchmark.h"

using namespace xdg;

int main(int argc, char** argv)
{
  argparse::ArgumentParser args("XDG Raytracing throughput benchmarking tool",
                                "1.0",
                                argparse::default_arguments::help);

  args.add_argument("filename")
    .help("Path to the input file");

  args.add_argument("volume")
    .help("Volume ID to query")
    .scan<'i', int>();

  args.add_argument("-n", "--num-rays")
    .default_value<std::uint32_t>(50'000'000)
    .help("Number of rays to cast for the benchmark")
    .scan<'u', std::uint32_t>();

  args.add_argument("-s", "--seed")
    .default_value<std::uint32_t>(12345)
    .help("Seed for random ray generation")
    .scan<'u', std::uint32_t>();

  args.add_argument("--warmup-rays")
    .default_value<std::uint32_t>(100'000)
    .help("Number of rays in an untimed warm-up launch; zero disables warm-up")
    .scan<'u', std::uint32_t>();

  args.add_argument("--trace-repetitions")
    .default_value<std::uint32_t>(1)
    .help("Number of timed launches of the same ray batch")
    .scan<'u', std::uint32_t>();

  args.add_argument("-o", "-p", "--origin", "--position")
    .help("Ray origin/position. Defaults to the center of the selected volume bounding box")
    .scan<'g', double>()
    .nargs(3);

  args.add_argument("--volume-center")
    .default_value(false)
    .implicit_value(true)
    .help("Use the queried volume bounding box center as the ray origin (the default when --origin is omitted)");

  args.add_argument("-m", "--mesh-library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

  args.add_argument("-rt", "--rt-library")
    .help("Ray tracing library to use. Currently implemented: EMBREE, GPRT, CUBQL")
    .default_value("EMBREE");

  args.add_argument("-l", "--list")
    .default_value(false)
    .implicit_value(true)
    .help("List all volumes in the file and exit");

  args.add_argument("-sr", "--source-radius")
    .default_value(0.0)
    .help("Radius of a scattered source around the origin")
    .scan<'g', double>();

  args.add_argument("--format")
    .default_value("human")
    .choices("human", "csv")
    .help("stdout format. Human readable (default) or csv");

  args.add_description(
    "Benchmarks ray-fire throughput for a selected mesh volume. A source "
    "position is provided and ray directions are randomly generated from it.");

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    exit(0);
  }

  std::string mesh_str = args.get<std::string>("--mesh-library");
  std::string rt_str = args.get<std::string>("--rt-library");
  std::string rt_label = rt_str;

  RTLibrary rt_lib;
  if (rt_str == "EMBREE") {
    rt_lib = RTLibrary::EMBREE;
  } else if (rt_str == "GPRT") {
    rt_lib = RTLibrary::GPRT;
  } else if (rt_str == "CUBQL") {
    rt_lib = RTLibrary::CUBQL;
  } else {
    fatal_error("Ray tracing library '{}' is not implemented in this benchmark tool yet", rt_str);
  }

  MeshLibrary mesh_lib;
  if (mesh_str == "MOAB") {
    mesh_lib = MeshLibrary::MOAB;
  } else if (mesh_str == "LIBMESH") {
    mesh_lib = MeshLibrary::LIBMESH;
  } else {
    fatal_error("Invalid mesh library '{}' specified", mesh_str);
  }

  const MeshID volume = args.get<int>("volume");
  const std::string model_filename = args.get<std::string>("filename");
  const std::string model_name = std::filesystem::path(model_filename).filename().string();
  const std::size_t num_rays = args.get<std::uint32_t>("--num-rays");
  const std::size_t requested_warmup_rays =
    args.get<std::uint32_t>("--warmup-rays");
  const std::uint32_t trace_repetitions =
    args.get<std::uint32_t>("--trace-repetitions");
  const std::uint32_t seed = args.get<std::uint32_t>("--seed");
  const double source_radius = args.get<double>("--source-radius");
  const std::string output_format = args.get<std::string>("--format");

  Timer wall_timer;
  Timer setup_timer;
  Timer generation_timer;
  Timer upload_timer;
  Timer trace_timer;
  Timer download_timer;

  wall_timer.start();

  // XDG setup and ray tracer initialisation
  setup_timer.start();
  std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file(model_filename);
  mesh_manager->init();

  if (args.get<bool>("--list")) {
    std::cout << "[" << fmt::format("{}", fmt::join(mesh_manager->volumes(), ", ")) << "]\n";
    return 0;
  }

  const auto origin_arg = args.present<std::vector<double>>("--origin");
  bool use_volume_center = args.get<bool>("--volume-center");
  if (origin_arg && use_volume_center) {
    warning("--volume-center enabled but an explicit origin was also provided. The explicit origin will be used and the volume center will be ignored.");
    use_volume_center = false;
  }

  Position origin = mesh_manager->volume_bounding_box(volume).center();
  if (origin_arg) {
    origin = Position(origin_arg.value());
  } else if (use_volume_center) {
    origin = mesh_manager->volume_bounding_box(volume).center();
  }

  xdg->prepare_volume_for_raytracing(volume);
  xdg->ray_tracing_interface()->init();

  const bool origin_in_volume = xdg->point_in_volume(volume, origin);
  if (!origin_in_volume) {
    const std::string origin_label = source_radius > 0.0 ? "source center" : "ray origin";
    warning(fmt::format("The {} ({}, {}, {}) is not inside volume {}. Results may be skewed by fewer intersections with the BVH.",
                        origin_label, origin.x, origin.y, origin.z, volume));
  } else if (source_radius > 0.0 && rt_lib == RTLibrary::EMBREE) {
    const double nearest_surface_distance = xdg->closest_distance(volume, origin);
    if (source_radius > nearest_surface_distance) {
      warning(fmt::format("The source radius ({}) is larger than the nearest surface distance ({}) from the source center to volume {}. "
                          "Some sampled source points may be outside the volume.",
                          source_radius, nearest_surface_distance, volume));
    }
  } else if (source_radius > 0.0) {
    warning("Source-radius containment validation is unavailable for the selected ray tracing backend");
  }

  setup_timer.stop();

  const auto num_faces = mesh_manager->num_volume_faces(volume);
  std::size_t num_hits = 0;
  if (num_rays < 1) fatal_error("Number of rays must be greater than 0");
  if (trace_repetitions < 1) {
    fatal_error("Number of trace repetitions must be greater than 0");
  }
  const std::size_t warmup_rays = std::min(num_rays, requested_warmup_rays);
  const std::uint64_t total_ray_queries =
    static_cast<std::uint64_t>(num_rays) * trace_repetitions;

  // Generate one host-side ray workload. Timed repetitions replay this batch
  // so the measurement isolates steady-state traversal from ray generation.
  generation_timer.start();
  std::vector<XDGRayHit> ray_hits(num_rays);

  #pragma omp parallel for schedule(runtime)
  for (std::size_t i = 0; i < num_rays; ++i) {
    std::uint32_t state = seed ^ static_cast<std::uint32_t>(i);
    const auto sample = tools::benchmark::random_spherical_source(origin.x,
                                                                   origin.y,
                                                                   origin.z,
                                                                   state,
                                                                   source_radius);

    XDGRayHit ray_hit {};
    ray_hit.origin[0] = sample.position[0];
    ray_hit.origin[1] = sample.position[1];
    ray_hit.origin[2] = sample.position[2];
    ray_hit.direction[0] = sample.direction[0];
    ray_hit.direction[1] = sample.direction[1];
    ray_hit.direction[2] = sample.direction[2];
    ray_hit.t_min = 0.0;
    ray_hit.t_max = INFTY;
    ray_hit.volume = volume;
    ray_hit.last_hit_primitive = ID_NONE;
    ray_hit.distance = INFTY;
    ray_hit.surface = ID_NONE;
    ray_hit.primitive = ID_NONE;
    ray_hit.point_in_volume = OUTSIDE;
    ray_hit.next_volume = ID_NONE;
    ray_hit.boundary_condition = static_cast<int32>(SurfaceBoundaryCondition::UNSET);
    ray_hits[i] = ray_hit;
  }
  generation_timer.stop();

  if (rt_lib == RTLibrary::EMBREE) {
    rt_label += " (" + std::to_string(XDGConfig::config().n_threads())
             + " CPU threads)";

    // Warm the worker threads and geometry cache without including this work
    // in the reported trace time.
    #pragma omp parallel for schedule(runtime)
    for (std::size_t i = 0; i < warmup_rays; ++i) {
      const auto& ray_hit = ray_hits[i];
      xdg->ray_fire(
        volume,
        Position(ray_hit.origin[0], ray_hit.origin[1], ray_hit.origin[2]),
        Direction(ray_hit.direction[0], ray_hit.direction[1], ray_hit.direction[2]));
    }

    trace_timer.start();
    for (std::uint32_t repetition = 0;
         repetition < trace_repetitions;
         ++repetition) {
      #pragma omp parallel for schedule(runtime)
      for (std::size_t i = 0; i < num_rays; ++i) {
        auto& ray_hit = ray_hits[i];
        const auto hit = xdg->ray_fire(
          volume,
          Position(ray_hit.origin[0], ray_hit.origin[1], ray_hit.origin[2]),
          Direction(ray_hit.direction[0], ray_hit.direction[1], ray_hit.direction[2]));
        ray_hit.distance = hit.first;
        ray_hit.surface = hit.second;
      }
    }
    trace_timer.stop();

    // Count hits outside of timing region
    for (const auto& ray_hit : ray_hits) {
      if (ray_hit.surface != ID_NONE) num_hits++;
    }
  }
  else if (rt_lib == RTLibrary::GPRT || rt_lib == RTLibrary::CUBQL) {
    XDGRayHitBuffer device_ray_hits = xdg->allocate_ray_hits(num_rays);

    upload_timer.start();
    xdg->upload_ray_hits(device_ray_hits, ray_hits.data(), ray_hits.size());
    upload_timer.stop();

    // The first GPRT batch binds the device buffer into the shader binding
    // table. Performing a small launch here keeps that one-time work, along
    // with normal device warm-up, outside the traversal measurement.
    if (warmup_rays > 0) {
      XDGRayHitBuffer warmup_buffer = device_ray_hits;
      warmup_buffer.count = warmup_rays;
      xdg->ray_fire_batch(warmup_buffer);
    }

    trace_timer.start();
    for (std::uint32_t repetition = 0;
         repetition < trace_repetitions;
         ++repetition) {
      xdg->ray_fire_batch(device_ray_hits);
    }
    trace_timer.stop();

    download_timer.start();
    xdg->download_ray_hits(device_ray_hits, ray_hits.data(), ray_hits.size());
    download_timer.stop();

    // Count hits outside of the timed query stages.
    for (const auto& ray_hit : ray_hits) {
      if (ray_hit.surface != ID_NONE) num_hits++;
    }

    xdg->free_ray_hits(device_ray_hits);
  }

  const std::size_t num_misses = num_rays - num_hits;
  const double hit_fraction = num_rays > 0
    ? static_cast<double>(num_hits) / static_cast<double>(num_rays)
    : 0.0;

  const double generation_time = generation_timer.elapsed();
  const double upload_time = upload_timer.elapsed();
  const double trace_time = trace_timer.elapsed();
  const double download_time = download_timer.elapsed();
  const double end_to_end_time = generation_time + trace_time;
  const double transfer_inclusive_time = generation_time + upload_time
                                      + trace_time + download_time;
  const double setup_time = setup_timer.elapsed();
  const double trace_only_rps = trace_time > 0.0
    ? static_cast<double>(total_ray_queries) / trace_time
    : 0.0;
  const double end_to_end_rps = end_to_end_time > 0.0
    ? static_cast<double>(total_ray_queries) / end_to_end_time
    : 0.0;
  const double transfer_inclusive_rps = transfer_inclusive_time > 0.0
    ? static_cast<double>(total_ray_queries) / transfer_inclusive_time
    : 0.0;

  wall_timer.stop();
  const double wall_time = wall_timer.elapsed();

  const std::vector<std::string> csv_columns {
    "model",
    "mesh_library",
    "rt_library",
    "volume",
    "num_faces",
    "num_rays",
    "warmup_rays",
    "trace_repetitions",
    "total_ray_queries",
    "num_hits",
    "num_misses",
    "hit_fraction",
    "seed",
    "source_radius",
    "origin_x",
    "origin_y",
    "origin_z",
    "n_threads",
    "initialisation_time_s",
    "generation_time_s",
    "upload_time_s",
    "trace_time_s",
    "download_time_s",
    "generation_trace_time_s",
    "transfer_inclusive_time_s",
    "end_to_end_throughput_rays_per_s",
    "transfer_inclusive_throughput_rays_per_s",
    "trace_only_throughput_rays_per_s",
    "wall_time_s"
  };

  const std::vector<std::string> csv_values {
    model_name,
    mesh_str,
    rt_str,
    fmt::format("{}", volume),
    fmt::format("{}", num_faces),
    fmt::format("{}", num_rays),
    fmt::format("{}", warmup_rays),
    fmt::format("{}", trace_repetitions),
    fmt::format("{}", total_ray_queries),
    fmt::format("{}", num_hits),
    fmt::format("{}", num_misses),
    fmt::format("{}", hit_fraction),
    fmt::format("{}", seed),
    fmt::format("{}", source_radius),
    fmt::format("{}", origin.x),
    fmt::format("{}", origin.y),
    fmt::format("{}", origin.z),
    fmt::format("{}", XDGConfig::config().n_threads()),
    fmt::format("{}", setup_time),
    fmt::format("{}", generation_time),
    fmt::format("{}", upload_time),
    fmt::format("{}", trace_time),
    fmt::format("{}", download_time),
    fmt::format("{}", end_to_end_time),
    fmt::format("{}", transfer_inclusive_time),
    fmt::format("{}", end_to_end_rps),
    fmt::format("{}", transfer_inclusive_rps),
    fmt::format("{}", trace_only_rps),
    fmt::format("{}", wall_time)
  };

  if (output_format == "csv") {
    std::cout << fmt::format("{}\n", fmt::join(csv_columns, ","));
    std::cout << fmt::format("{}\n", fmt::join(csv_values, ","));
  } else {
    std::cout << "\nXDG ray benchmark results\n";
    std::cout << "----------------------------------------\n";
    std::cout << "Model                 : " << model_name << "\n";
    std::cout << "Mesh library          : " << mesh_str << "\n";
    std::cout << "Ray tracing library   : " << rt_label << "\n";
    std::cout << "Volume                : " << volume << "\n";
    std::cout << "Volume faces          : " << num_faces << "\n";
    std::cout << "Seed                  : " << seed << "\n";
    std::cout << "Rays per batch        : " << num_rays << "\n";
    std::cout << "Warm-up rays          : " << warmup_rays << " (untimed)\n";
    std::cout << "Trace repetitions     : " << trace_repetitions << "\n";
    std::cout << "Total ray queries     : " << total_ray_queries << "\n";
    if (source_radius != 0.0) {
      std::cout << "Source center         : "
                << origin.x << ", " << origin.y << ", " << origin.z << "\n";
      std::cout << "Source radius         : " << source_radius << "\n";
    } else {
      std::cout << "Origin (fixed)        : "
                << origin.x << ", " << origin.y << ", " << origin.z << "\n";
    }
    std::cout << "----------------------------------------\n";
    std::cout << "Hits                  : " << num_hits << "\n";
    std::cout << "Misses                : " << num_misses << "\n";
    std::cout << "Hit fraction          : " << hit_fraction << "\n";
    std::cout << "----------------------------------------\n";
    std::cout << "Initialisation time   : " << setup_time << " s\n";
    std::cout << "Ray generation time   : " << generation_time << " s\n";
    std::cout << "Upload time           : " << upload_time << " s\n";
    std::cout << "Ray tracing time      : " << trace_time << " s\n";
    std::cout << "Download time         : " << download_time << " s\n";
    std::cout << "Generation + tracing  : " << end_to_end_time
              << " s (transfers excluded)\n";
    std::cout << "Transfer-inclusive    : " << transfer_inclusive_time << " s\n";
    std::cout << "Full wall-clock time  : " << wall_time << " s\n";
    std::cout << "----------------------------------------\n";
    std::cout << "Generation + trace    : " << end_to_end_rps
              << " rays/s (transfers excluded)\n";
    std::cout << "Transfer-inclusive    : " << transfer_inclusive_rps << " rays/s\n";
    std::cout << "Trace-only throughput : " << trace_only_rps << " rays/s\n";
  }

  return 0;
}
