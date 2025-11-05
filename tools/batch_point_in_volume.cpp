#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

using namespace xdg;

int main(int argc, char** argv) {

  argparse::ArgumentParser args("XDG Batch Point In Volume Tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
    .help("Path to the input file");

  args.add_argument("volume")
    .help("Volume ID to query").scan<'i', int>();

  args.add_argument("-l", "--list")
    .default_value(false)
    .implicit_value(true)
    .help("List all volumes in the file and exit");

  args.add_argument("-o", "-p", "--origin", "--position")
    .default_value(std::vector<double>{0.0, 0.0, 0.0})
    .help("Ray origin/position. Repeat to supply multiple origins.")
    .scan<'g', double>().nargs(3).append();

  args.add_argument("-d", "--direction")
    .default_value(std::vector<double>{0.0, 0.0, 1.0})
    .help("Ray direction. Repeat to supply multiple directions.")
    .scan<'g', double>().nargs(3).append();


  args.add_argument("-m", "--mesh-library")
      .help("Mesh library to use. One of (MOAB, LIBMESH)")
      .default_value("MOAB");

  args.add_argument("-r", "--rt-library")
      .help("Ray tracing library to use. One of (EMBREE, GPRT)")
      .default_value("GPRT");

    // High-level rules in the description
  args.add_description(
    "Directions are completely optional for this tool but the number provided will effect how the program runs: \n\n"
    "  Only points (mask all, device default dir used)\n"
    "     --origin 0 0 0 --origin 5.1 0 0 --origin 0 0 0\n\n"
    "  One direction (broadcast to all)\n"
    "     --origin 0 0 0 --origin 5.1 0 0 --direction 1 0 0\n\n"
    "  Several directions. Match to points and mask remainder\n"
    "     --origin 0 0 0 --origin 5.1 0 0 --origin 4.999999 0 0 \\\n"
    "     --direction 1 0 0 --direction -1 0 0\n"
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
  std::string rt_str   = args.get<std::string>("--rt-library");

  MeshLibrary mesh_lib;
  if (mesh_str == "MOAB") mesh_lib = MeshLibrary::MOAB;
  else if (mesh_str == "LIBMESH") fatal_error("LibMesh is not currently supported with GPRT");
  else fatal_error("Invalid mesh library '{}' specified", mesh_str);

  RTLibrary rt_lib;
  if (rt_str == "EMBREE") rt_lib = RTLibrary::EMBREE;
  else if (rt_str == "GPRT") rt_lib = RTLibrary::GPRT;
  else fatal_error("Invalid ray tracing library '{}' specified", rt_str);

  // create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();
  mm->parse_metadata();

  auto rti = xdg->ray_tracing_interface();

  if (args.get<bool>("--list")) {
    std::cout << "Volumes: " << std::endl;
    for (auto volume : mm->volumes()) {
      std::cout << volume << std::endl;
    }
    exit(0);
  }

  MeshID volume = args.get<int>("volume");
  xdg->prepare_volume_for_raytracing(volume);

  // Gather our inputs and determine which mode of operation the tool will be working in 
  auto flat_origins    = args.get<std::vector<double>>("--origin");
  auto flat_directions = args.get<std::vector<double>>("--direction");

  if (flat_origins.empty()) {
    fatal_error("You must supply at least one --origin x y z");
  }
  if (flat_origins.size() % 3 != 0) {
    fatal_error("Origins must be supplied in groups of 3 numbers.");
  }

  // group every 3 into Position / Direction
  std::vector<std::vector<double>> args_origins;
  for (size_t i = 0; i < flat_origins.size(); i += 3) {
    args_origins.push_back({flat_origins[i], flat_origins[i+1], flat_origins[i+2]});
  }

  std::vector<std::vector<double>> args_directions;
  for (size_t i = 0; i < flat_directions.size(); i += 3) {
    args_directions.push_back({flat_directions[i], flat_directions[i+1], flat_directions[i+2]});
  }

  // helper lambdas to convert std::vector to xdg::Position and xdg::Direction types 
  auto vec_to_pos = [](const std::vector<double>& v) { return Position{v[0], v[1], v[2]}; };
  auto vec_to_dir = [](const std::vector<double>& v) { 
    Direction dir{v[0], v[1], v[2]}; 
    dir.normalize();
    return dir;
  };

  const size_t N = args_origins.size();
  size_t num_dirs = args_directions.size();

  std::vector<Position> origins;
  origins.reserve(N);
  for (const auto& o : args_origins) origins.push_back(vec_to_pos(o));

  std::vector<Direction> directions;
  std::vector<uint8_t> has_dir; // mask to indicate which rays have directions
  const Direction* directions_ptr = nullptr;
  const uint8_t* has_dir_ptr = nullptr;

  if (num_dirs == 0) {
    // No directions let batch API set default direction per point
    directions_ptr = nullptr;
    has_dir_ptr = nullptr;
  } else if (num_dirs == 1) {
    // Broadcast one direction to all points (no mask needed)
    directions.assign(N, vec_to_dir(args_directions[0]));
    directions_ptr = directions.data();
    has_dir_ptr = nullptr;
  } else if (num_dirs < N) {
    // First k get explicit directions; rest fall back to default via mask
    const size_t k = num_dirs;
    directions.resize(N);
    has_dir.assign(N, 0);
    for (size_t i = 0; i < k; ++i) {
      directions[i] = vec_to_dir(args_directions[i]);
      has_dir[i] = 1;
    }
    directions_ptr = directions.data();
    has_dir_ptr = has_dir.data();
  } else {
    // ≥ N directions → use first N pairwise (no mask needed)
    directions.reserve(N);
    for (size_t i = 0; i < N; ++i) directions.push_back(vec_to_dir(args_directions[i]));
    directions_ptr = directions.data();
    has_dir_ptr = nullptr;
  }

  std::vector<uint8_t> results(N, 0xFF);

  xdg->batch_point_in_volume(volume,
                             origins.data(),
                             directions.data(),
                             N,
                             results.data(),
                             has_dir.data());
  
  std::cout << std::endl << "Printing Batch point in volume results..." << std::endl;

  std::cout << "\nPrinting Batch point-in-volume results...\n";
  for (size_t i = 0; i < N; ++i) {
    const auto& p = origins[i];
    std::cout << "Point (" << p.x << ", " << p.y << ", " << p.z << ") "
              << (results[i] ? "is in " : "is NOT in ")
              << "Volume " << volume << "\n";
  }

  return 0;
}
