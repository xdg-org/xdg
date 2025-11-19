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

#include "argparse/argparse.hpp"

using namespace xdg;

inline Direction random_unit_dir(std::mt19937_64 &rng) {
  std::uniform_real_distribution<double> U(-1.0, 1.0);
  double x1, x2, s;
  do {
    x1 = U(rng);
    x2 = U(rng);
    s  = x1*x1 + x2*x2;
  } while (s <= 0.0 || s >= 1.0);
  const double t = 2.0 * std::sqrt(1.0 - s);
  return { x1 * t, x2 * t, 1.0 - 2.0 * s }; // already unit length
}

inline void generate_dirs(std::vector<Direction> &out, uint64_t seed = 12345) {
  std::mt19937_64 rng(seed);
  for (auto &d : out) d = random_unit_dir(rng);
}

int main(int argc, char** argv) {

  argparse::ArgumentParser args("XDG Ray Tracing throughput benchmarking tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
    .help("Path to the input file");

  args.add_argument("volume")
    .help("Volume ID to query")
    .scan<'i', int>();

  args.add_argument("-n", "--num-rays")
    .default_value<std::size_t>(10'000'000)
    .help("Number of rays to be cast for the benchmark (default - 10 million)")
    .scan<'u', std::size_t>();

  args.add_argument("-s", "--seed")
    .default_value<uint64_t>(12345)
    .help("Seed for random number generator (default - 12345)")
    .scan<'u', uint64_t>();

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
  // mm->parse_metadata();

  // Generate a set of random rays
  size_t N = args.get<uint64_t>("--num-rays");
  uint64_t seed = args.get<uint64_t>("--seed");
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


  if (rt_lib == RTLibrary::GPRT) {
    // GPRT backend supports batch ray fire
    xdg->ray_fire(volume, origins.data(), directions.data(), N, hitDistances.data(), hitElements.data());
  } else {
    for (size_t i = 0; i < N; ++i) {
      auto result = xdg->ray_fire(volume, origin, directions[i]);
    }
  }
  
  return 0;
}