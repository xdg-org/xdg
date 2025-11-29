#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <cmath>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"
#include "xdg/ray_tracers.h"
#include "xdg/timer.h"

#include "argparse/argparse.hpp"

// GPRT includes
#include "gprt/gprt.h"
#include "ray_benchmark_shared.h"

using namespace xdg;
extern GPRTProgram ray_benchmark_deviceCode;

inline double rand01(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return double(state) * (1.0 / 4294967296.0);
}

inline Direction random_unit_dir_lcg(uint32_t &state)
{
  double x1, x2, s;
  do {
    x1 = rand01(state) * 2.0 - 1.0;
    x2 = rand01(state) * 2.0 - 1.0;
    s  = x1 * x1 + x2 * x2;
  } while (s <= 0.0 || s >= 1.0);

  double t = 2.0 * std::sqrt(1.0 - s);
  return { x1 * t, x2 * t, 1.0 - 2.0 * s };
}

inline void generate_dirs(std::vector<Direction> &out, uint32_t seed)
{
  for (uint32_t i = 0; i < out.size(); ++i) {
    uint32_t state = seed ^ i;
    out[i] = random_unit_dir_lcg(state);
  }
}

int main(int argc, char** argv) {

  argparse::ArgumentParser args("XDG Ray Tracing throughput benchmarking tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
    .help("Path to the input file");

  args.add_argument("volume")
    .help("Volume ID to query")
    .scan<'i', int>();

  args.add_argument("-n", "--num-rays")
    .default_value<uint32_t>(10'000'000)
    .help("Number of rays to be cast for the benchmark (default - 10 million)")
    .scan<'u', uint32_t>();

  args.add_argument("-s", "--seed")
    .default_value<uint32_t>(12345)
    .help("Seed for random number generator (default - 12345)")
    .scan<'u', uint32_t>();

  args.add_argument("-o", "-p", "--origin", "--position")
    .default_value(std::vector<double>{0.0, 0.0, 0.0})
    .help("Ray origin/position (default - {0.0, 0.0, 0.0} )")
    .scan<'g', double>().nargs(3);

  args.add_argument("-m", "--mesh-library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

  args.add_argument("-r", "--rt-library")
    .help("Ray tracing library to use. One of (EMBREE, GPRT)")
    .default_value("EMBREE");

  args.add_argument("-l", "--list")
    .default_value(false)
    .implicit_value(true)
    .help("List all volumes in the file and exit");

  args.add_description(
    "This tool supports can be used to benchmark XDG ray tracing throughput on a given mesh against"
    "a given volume \n."
    "A single origin/seed point is provided and ray directions are randomly generated in 360 degrees from that position"
  );

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    return 1;
  }

  std::string mesh_str = args.get<std::string>("--mesh-library");
  std::string rt_str   = args.get<std::string>("--rt-library");

  RTLibrary rt_lib;
  if (rt_str == "EMBREE")
    rt_lib = RTLibrary::EMBREE;
  else if (rt_str == "GPRT")
    rt_lib = RTLibrary::GPRT;
  else
    fatal_error("Invalid ray tracing library '{}' specified", rt_str);

  MeshLibrary mesh_lib;
  if (mesh_str == "MOAB") {
    mesh_lib = MeshLibrary::MOAB;
  } else if (mesh_str == "LIBMESH") {
    mesh_lib = MeshLibrary::LIBMESH;
    if (rt_lib == RTLibrary::GPRT)
      fatal_error("LibMesh is not currently supported with GPRT");
  } else {
    fatal_error("Invalid mesh library '{}' specified", mesh_str);
  }

  // Full wall-clock timer (post-argparse)
  Timer wall_timer;
  wall_timer.start();

  // Separate timers for setup, generation, and tracing
  Timer setup_timer;
  Timer gen_timer;
  Timer trace_timer;

  // --------------------------
  // XDG setup timing
  // --------------------------
  setup_timer.start();

  std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();

  MeshID volume = args.get<int>("volume");
  xdg->prepare_raytracer();
  xdg->prepare_volume_for_raytracing(volume);
  auto rti = xdg->ray_tracing_interface();

  setup_timer.stop();

  std::size_t N = args.get<uint32_t>("--num-rays");
  uint32_t seed = args.get<uint32_t>("--seed");
  Position origin = args.get<std::vector<double>>("--origin");

  std::cout << "Volume ID: " << volume << " with: "
            << mm->num_volume_faces(volume) << " faces" << std::endl;

  std::cout << "Starting ray fire benchmark with " << N << " rays"
            << " using " << rt_str << ": \n" << std::endl;

  std::cout << "XDG initalisation Time = " << setup_timer.elapsed() << "s" << std::endl;

  // --------------------------
  // Backend-specific sections
  // --------------------------

  if (rt_lib == RTLibrary::GPRT) {
    // One-time GPRT compute setup (not timed as generation)
    auto gprt_rti = std::dynamic_pointer_cast<xdg::GPRTRayTracer>(rti);
    if (!gprt_rti)
      fatal_error("Failed to cast RayTracer to GPRTRayTracer");

    GPRTContext context = gprt_rti->context();
    GPRTModule module   = gprtModuleCreate(context, ray_benchmark_deviceCode);
    auto genRandomRays  = gprtComputeCreate<GenerateRandomRayParams>(
                            context, module, "generate_random_rays");

    // ---- Random ray generation on device ----
    gen_timer.start();

    auto rayHitBuffers = gprt_rti->get_device_rayhit_buffers(N);

    constexpr int threadsPerGroup = 64;
    const int neededGroups = (int)((N + threadsPerGroup - 1) / threadsPerGroup);
    const int groups       = std::min(neededGroups, WORKGROUP_LIMIT);

    GenerateRandomRayParams randomRayParams = {};
    randomRayParams.rays          = rayHitBuffers.rayDevPtr; // xdg::dblRay* on device
    randomRayParams.numRays       = (uint32_t)N;
    randomRayParams.origin        = { origin.x, origin.y, origin.z };
    randomRayParams.seed          = seed;
    randomRayParams.total_threads = (uint32_t)(groups * threadsPerGroup);

    gprtComputeLaunch(genRandomRays,
                      { (uint32_t)groups, 1, 1 },
                      { (uint32_t)threadsPerGroup, 1, 1 },
                      randomRayParams);
    gprtComputeSynchronize(context);

    gen_timer.stop();
    std::cout << "Random ray generation (via external compute shader) Time = "
              << gen_timer.elapsed() << "s" << std::endl;

    // ---- Ray tracing on device ----
    trace_timer.start();
    xdg->ray_fire_packed(volume, N); // ray_fire against pre-packed rays on device
    trace_timer.stop();

  } else {
    // EMBREE / CPU backend

    // ---- Random ray generation on host ----
    gen_timer.start();
    std::vector<Direction> directions(N);
    generate_dirs(directions, seed);
    gen_timer.stop();
    std::cout << "Random ray generation Time = "
              << gen_timer.elapsed() << "s" << std::endl;

    // ---- Ray tracing on host ----
    trace_timer.start();
    for (std::size_t i = 0; i < N; ++i) {
      auto result = xdg->ray_fire(volume, origin, directions[i]);
      (void)result; // suppress unused warning
    }
    trace_timer.stop();
  }

  // --------------------------
  // Final reporting
  // --------------------------
  double setup_time = setup_timer.elapsed();
  double gen_time   = gen_timer.elapsed();
  double trace_time = trace_timer.elapsed();

  double trace_only_rps = (trace_time > 0.0)
    ? static_cast<double>(N) / trace_time
    : 0.0;

  double end_to_end_time = gen_time + trace_time;
  double end_to_end_rps  = (end_to_end_time > 0.0)
    ? static_cast<double>(N) / end_to_end_time
    : 0.0;

  wall_timer.stop();
  double wall_time = wall_timer.elapsed();

  std::cout << "Generation + tracing time    = " << end_to_end_time
            << "s" << std::endl;
  std::cout << "End-to-end throughput        = " << end_to_end_rps
            << " rays/s" << std::endl;
  std::cout << "Full wall-clock time         = " << wall_time
            << "s (post-argparse)" << std::endl;

  std::cout << "----------------------------------------" << std::endl;
  std::cout << "Ray Tracing Time (trace-only) = " << trace_time
            << "s for " << N << " rays" << std::endl;
  std::cout << "Trace-only throughput        = " << trace_only_rps
            << " rays/s" << std::endl;
  std::cout << "---------------------------------------- \n" << std::endl;
  return 0;
}
