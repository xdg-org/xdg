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

#ifdef XDG_ENABLE_CUBQL
#include <omp.h>
#include "xdg/cuBQL/intersection.h"
#include "xdg/cuBQL/ray_tracer.h"
#endif

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
    .default_value<std::uint32_t>(10'000'000)
    .help("Number of rays to cast for the benchmark")
    .scan<'u', std::uint32_t>();

  args.add_argument("-s", "--seed")
    .default_value<std::uint32_t>(12345)
    .help("Seed for random ray generation")
    .scan<'u', std::uint32_t>();

  args.add_argument("-o", "-p", "--origin", "--position")
    .help("Ray origin/position. Defaults to the center of the model bounding box")
    .scan<'g', double>()
    .nargs(3);

  args.add_argument("--volume-center")
    .default_value(false)
    .implicit_value(true)
    .help("Use the queried volume bounding box center as the ray origin");

  args.add_argument("-m", "--mesh-library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

  args.add_argument("-rt", "--rt-library")
    .help("Ray tracing library to use. Currently implemented: EMBREE, CUBQL")
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
  const std::uint32_t seed = args.get<std::uint32_t>("--seed");
  const double source_radius = args.get<double>("--source-radius");
  const std::string output_format = args.get<std::string>("--format");

  Timer wall_timer;
  Timer setup_timer;
  Timer generation_timer;
  Timer trace_timer;

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

  Position origin = mesh_manager->global_bounding_box().center();
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
  } else {
    const double nearest_surface_distance = xdg->closest_distance(volume, origin);
    if (source_radius > nearest_surface_distance) {
      warning(fmt::format("The source radius ({}) is larger than the nearest surface distance ({}) from the source center to volume {}. "
                          "Some sampled source points may be outside the volume.",
                          source_radius, nearest_surface_distance, volume));
    }
  }

  setup_timer.stop();

  const auto num_faces = mesh_manager->num_volume_faces(volume);
  std::size_t num_hits = 0;
  if (num_rays < 1) fatal_error("Number of rays must be greater than 0");

  if (rt_lib == RTLibrary::EMBREE) {
    rt_label += " (" + std::to_string(XDGConfig::config().n_threads())
             + " CPU threads)";

    // Generate random rays from source
    generation_timer.start();
    std::vector<Position> origins(num_rays);
    std::vector<Direction> directions(num_rays);

    #pragma omp parallel for schedule(runtime)
    for (std::size_t i = 0; i < num_rays; ++i) {
      std::uint32_t state = seed ^ static_cast<std::uint32_t>(i);
      auto sample = tools::benchmark::random_spherical_source(origin.x,
                                                              origin.y,
                                                              origin.z,
                                                              state,
                                                              source_radius);
      origins[i] = Position(sample.position[0],
                            sample.position[1],
                            sample.position[2]);
      directions[i] = Direction(sample.direction[0],
                                sample.direction[1],
                                sample.direction[2]);
    }
    generation_timer.stop();

    std::vector<MeshID> hit_surfaces(num_rays, ID_NONE);

    trace_timer.start();
    #pragma omp parallel for schedule(runtime)
    for (std::size_t i = 0; i < num_rays; ++i) {
      hit_surfaces[i] = xdg->ray_fire(volume, origins[i], directions[i]).second; // just return surface id of hit
    }
    trace_timer.stop();

    // Count hits outside of timing region
    for (std::size_t i = 0; i < num_rays; ++i) {
      if (hit_surfaces[i] != ID_NONE) num_hits++;
    }
  }
  else if (rt_lib == RTLibrary::CUBQL) {
    #ifndef XDG_ENABLE_CUBQL
      fatal_error("This build was not compiled with cuBQL support (XDG_ENABLE_CUBQL=OFF).");
    #else
    auto rti = std::dynamic_pointer_cast<CuBQLRayTracer>(xdg->ray_tracing_interface());

      // Generate random rays directly on the target device.
      generation_timer.start();

      const int gpu_id = omp_get_default_device();

      CuBQLRay* d_rays = static_cast<CuBQLRay*>(
        omp_target_alloc(num_rays * sizeof(CuBQLRay), gpu_id));

      if (!d_rays) {
        fatal_error("Failed to allocate cuBQL ray buffer");
      }

      const double origin_x = origin.x;
      const double origin_y = origin.y;
      const double origin_z = origin.z;

      #pragma omp target teams distribute parallel for device(gpu_id) is_device_ptr(d_rays)
      for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
        std::uint32_t state = seed ^ static_cast<std::uint32_t>(ray_id);

        auto sample = tools::benchmark::random_spherical_source(origin_x,
                                                                origin_y,
                                                                origin_z,
                                                                state,
                                                                source_radius);

        CuBQLRay ray;
        ray.origin = cuBQL::vec3d(sample.position[0],
                                  sample.position[1],
                                  sample.position[2]);
        ray.direction = cuBQL::vec3d(sample.direction[0],
                                     sample.direction[1],
                                     sample.direction[2]);
        ray.tMin = 0.0;
        ray.tMax = INFTY;
        ray.volume = volume;

        d_rays[ray_id] = ray;
      }

      CuBQLSurfaceHit* d_hits = static_cast<CuBQLSurfaceHit*>(
        omp_target_alloc(num_rays * sizeof(CuBQLSurfaceHit), gpu_id));

      if (!d_hits) {
        omp_target_free(d_rays, gpu_id);
        fatal_error("Failed to allocate cuBQL hit buffer");
      }

      generation_timer.stop();

      // Trace rays and count hits on the target device.
      trace_timer.start();
      rti->ray_fire_batch(d_rays, d_hits, num_rays);
      trace_timer.stop();

      #pragma omp target teams distribute parallel for device(gpu_id) \
        is_device_ptr(d_hits) reduction(+:num_hits)
      for (std::size_t ray_id = 0; ray_id < num_rays; ++ray_id) {
        if (d_hits[ray_id].primitive != ID_NONE) num_hits++;
      }

      omp_target_free(d_hits, gpu_id);
      omp_target_free(d_rays, gpu_id);
    #endif
  }

  const std::size_t num_misses = num_rays - num_hits;
  const double hit_fraction = num_rays > 0
    ? static_cast<double>(num_hits) / static_cast<double>(num_rays)
    : 0.0;

  const double generation_time = generation_timer.elapsed();
  const double trace_time = trace_timer.elapsed();
  const double end_to_end_time = generation_time + trace_time;
  const double setup_time = setup_timer.elapsed();
  const double trace_only_rps = trace_time > 0.0
    ? static_cast<double>(num_rays) / trace_time
    : 0.0;
  const double end_to_end_rps = end_to_end_time > 0.0
    ? static_cast<double>(num_rays) / end_to_end_time
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
    "trace_time_s",
    "generation_trace_time_s",
    "end_to_end_throughput_rays_per_s",
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
    fmt::format("{}", trace_time),
    fmt::format("{}", end_to_end_time),
    fmt::format("{}", end_to_end_rps),
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
    std::cout << "Rays                  : " << num_rays << "\n";
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
    std::cout << "Ray tracing time      : " << trace_time << " s\n";
    std::cout << "Generation + tracing  : " << end_to_end_time << " s\n";
    std::cout << "Full wall-clock time  : " << wall_time << " s\n";
    std::cout << "----------------------------------------\n";
    std::cout << "End-to-end throughput : " << end_to_end_rps << " rays/s\n";
    std::cout << "Trace-only throughput : " << trace_only_rps << " rays/s\n";
  }

  return 0;
}
