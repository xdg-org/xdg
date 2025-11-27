#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <cmath>
#include <chrono>


#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"
#include "xdg/ray_tracers.h"

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
    exit(0);
  }

  std::string mesh_str = args.get<std::string>("--mesh-library");
  std::string rt_str = args.get<std::string>("--rt-library");

  RTLibrary rt_lib;
  if (rt_str == "EMBREE")
  rt_lib = RTLibrary::EMBREE;
  else if (rt_str == "GPRT")
  rt_lib = RTLibrary::GPRT;
  else
  fatal_error("Invalid ray tracing library '{}' specified", rt_str);

  MeshLibrary mesh_lib;
  if (mesh_str == "MOAB")
  mesh_lib = MeshLibrary::MOAB;
  else if (mesh_str == "LIBMESH") {
  mesh_lib = MeshLibrary::LIBMESH;
  if (rt_lib == RTLibrary::GPRT)
      fatal_error("LibMesh is not currently supported with GPRT");
  }
  else
  fatal_error("Invalid mesh library '{}' specified", mesh_str);

  // create xdg instance
  std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();

  // Generate a set of random rays
  std::size_t N = args.get<uint32_t>("--num-rays");
  uint32_t seed = args.get<uint32_t>("--seed");
  Position origin = args.get<std::vector<double>>("--origin");
  std::vector<Position> origins(N, origin);
  std::vector<Direction> directions(N);
  generate_dirs(directions, seed);

  MeshID volume = args.get<int>("volume");
  xdg->prepare_raytracer();
  xdg->prepare_volume_for_raytracing(volume);
  auto rti = xdg->ray_tracing_interface();

  std::vector<double> hitDistances(N, -1.0);
  std::vector<MeshID> hitElements(N, ID_NONE);

  auto start = std::chrono::high_resolution_clock::now();
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  if (rt_lib == RTLibrary::GPRT) {
    // Create GPRT context for compute shader that generates rays
    auto gprt_rti = std::dynamic_pointer_cast<xdg::GPRTRayTracer>(rti);
    GPRTContext context = gprt_rti->context();
    GPRTModule module = gprtModuleCreate(context, ray_benchmark_deviceCode); 
    auto genRandomRays = gprtComputeCreate<GenerateRandomRayParams>(context, module, "generate_random_rays");
    
    // this exposes rayhit buffers from the GPRT/vulkan context avaiable within XDG
    auto rayHitBuffers = gprt_rti->get_device_rayhit_buffers(N);


    constexpr int threadsPerGroup = 64;
    const int neededGroups = (N + threadsPerGroup - 1) / threadsPerGroup;
    const int groups = std::min(neededGroups, WORKGROUP_LIMIT);

    GenerateRandomRayParams randomRayParams = {};
    randomRayParams.rays = rayHitBuffers.rayDevPtr; // assign xdg ray buffer pointer for compute shader
    randomRayParams.numRays = N;
    randomRayParams.origin = {origin.x, origin.y, origin.z};
    randomRayParams.seed = seed;
    randomRayParams.total_threads = groups * threadsPerGroup;

    gprtComputeLaunch(genRandomRays, {groups, 1, 1}, {threadsPerGroup, 1, 1}, randomRayParams);
    gprtComputeSynchronize(context);

    std::cout << "Volume ID: " << volume << " with: " << mm->num_volume_faces(volume)  
          << " faces" << std::endl;

    std::cout << "Starting ray fire benchmark with " << N << " rays"  << " using " 
              << rt_str << ": \n" << std::endl;
    start = std::chrono::high_resolution_clock::now();

    xdg->ray_fire_packed(volume, N); // ray_fire against pre-packed rays on device

    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    double rays_per_second = static_cast<double>(N) / elapsed.count();

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Completed " << N << " rays in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Ray tracing throughput: " << rays_per_second << " rays/second." << std::endl;
    std::cout << "---------------------------------------- \n" << std::endl;
  } else {
    std::cout << "Volume ID: " << volume << " with: " << mm->num_volume_faces(volume)  
          << " faces" << std::endl;

    std::cout << "Starting ray fire benchmark with " << N << " rays"  << " using " 
              << rt_str << ": \n" << std::endl;
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < N; ++i) {
      auto result = xdg->ray_fire(volume, origin, directions[i]);
    }
    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    double rays_per_second = static_cast<double>(N) / elapsed.count();

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Completed " << N << " rays in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Ray tracing throughput: " << rays_per_second << " rays/second." << std::endl;
    std::cout << "---------------------------------------- \n" << std::endl;
  }

  return 0;
}