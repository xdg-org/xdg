#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <iomanip>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

enum class BatchMode {
  ORIGIN_BROADCAST,    // 1 origin, many directions
  DIRECTION_BROADCAST, // many origins, 1 direction
  PAIRWISE             // equal numbers of origins and directions
};

inline const char* to_string(BatchMode mode) {
  switch (mode) {
    case BatchMode::ORIGIN_BROADCAST:    return "ORIGIN_BROADCAST";
    case BatchMode::DIRECTION_BROADCAST: return "DIRECTION_BROADCAST";
    case BatchMode::PAIRWISE:            return "PAIRWISE";
    default:                             return "UNKNOWN";
  }
}

inline BatchMode deduce_batch_mode(size_t num_origins, size_t num_directions) {
  if (num_origins == 0 || num_directions == 0) {
    throw std::runtime_error("At least one origin and one direction must be provided.");
  }

  if (num_origins == 1 && num_directions > 1) {
    return BatchMode::ORIGIN_BROADCAST;
  }
  else if (num_directions == 1 && num_origins > 1) {
    return BatchMode::DIRECTION_BROADCAST;
  }
  else if (num_origins == num_directions) {
    return BatchMode::PAIRWISE;
  }
  else {
    throw std::runtime_error(
      "Invalid combination: number of origins (" + std::to_string(num_origins) +
      ") does not match number of directions (" + std::to_string(num_directions) +
      ") for broadcast or pairwise mode."
    );
  }
}

using namespace xdg;

int main(int argc, char** argv) {

  argparse::ArgumentParser args("XDG Batch Ray Fire Tool", "1.0", argparse::default_arguments::help);

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
      "This tool supports two modes of operation for batch ray firing: 'Broadcast' and 'Pairwise'\n\n"
      "To use 'Broadcast' mode, provide one origin and many directions, or one direction and many origins:\n"
      "   --origin x y z --direction u1 v1 w1 --direction u2 v2 w2 ...\n"
      "   --direction u v w --origin x1 y1 z1 --origin x2 y2 z2 ...\n\n"
      "To use 'Pairwise' mode, each origin is paired with a corresponding direction in order:\n"
      "   --origin x1 y1 z1 --direction u1 v1 w1 --origin x2 y2 z2 --direction u2 v2 w2 ...\n"
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
  if (flat_directions.size() % 3 != 0) {
    fatal_error("Directions must be supplied in groups of 3 numbers.");
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

  size_t num_orig = args_origins.size();
  size_t num_dirs = args_directions.size();

  auto mode = deduce_batch_mode(num_orig, num_dirs);
  std::cout << "Running XDG Batch Ray Fire in " << to_string(mode) << " mode" << std::endl;
  std::vector<Position> origins;
  std::vector<Direction> directions;

  switch (mode)
  {
  case BatchMode::ORIGIN_BROADCAST:
    origins.assign(num_dirs, vec_to_pos(args_origins[0]));
    directions.reserve(num_dirs);
    for (const auto& dir : args_directions) directions.push_back(vec_to_dir(dir)); 
    break;
  case BatchMode::DIRECTION_BROADCAST:
    directions.assign(num_orig, vec_to_dir(args_directions[0]));
    origins.reserve(num_orig);
    for (const auto& origin : args_origins) origins.push_back(vec_to_pos(origin));
    break;
  case BatchMode::PAIRWISE: 
    origins.reserve(num_orig);
    directions.reserve(num_dirs);
    for (size_t i = 0; i < num_orig; ++i)
    {
      origins.push_back(vec_to_pos(args_origins[i]));
      directions.push_back(vec_to_dir(args_directions[i]));
    }
    break;

  default:
    fatal_error("You must provide either a single origin and many directions. "
                "A single direction and many origins. Or an equal number of origins and directions.");
  }

  size_t num_rays = origins.size(); // get number of rays to fire from now aligned arrays

  std::vector<double> hitDistances(num_rays);
  std::vector<MeshID> surfacesHit(num_rays);

  xdg->ray_fire(volume, origins.data(), directions.data(), num_rays, hitDistances.data(), surfacesHit.data());

  std::cout << std::endl << "Printing Batch Ray results..." << std::endl;

  for (size_t i = 0; i < num_rays; ++i) {
    std::cout << "Ray[" << i << "] "
              << "Origin=(" << origins[i].x << ", " << origins[i].y << ", " << origins[i].z << ") "
              << "Dir=(" << directions[i].x << ", " << directions[i].y << ", " << directions[i].z << ") "
              << "Distance=" << std::setprecision(17) << hitDistances[i] << " "
              << "| Surface=" << surfacesHit[i] << "\n";
  }

  return 0;
}
