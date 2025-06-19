#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

using namespace xdg;

int main(int argc, char** argv) {

  argparse::ArgumentParser args("XDG Ray Fire Tool", "1.0", argparse::default_arguments::help);

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
    .help("Ray origin/position").scan<'g', double>().nargs(3);

  args.add_argument("-d", "--direction")
    .default_value(std::vector<double>{0.0, 0.0, 1.0})
    .help("Ray direction").scan<'g', double>().nargs(3);

  args.add_argument("-m", "--mesh-library")
      .help("Mesh library to use. One of (MOAB, LIBMESH)")
      .default_value("MOAB");

  args.add_argument("-r", "--rt-library")
      .help("Ray tracing library to use. One of (EMBREE, GPRT)")
      .default_value("EMBREE");

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
  if (mesh_str == "MOAB")
    mesh_lib = MeshLibrary::MOAB;
  else if (mesh_str == "LIBMESH")
    fatal_error("LibMesh is not currently supported with GPRT");
    // mesh_lib = MeshLibrary::LIBMESH;
  else
    fatal_error("Invalid mesh library '{}' specified", mesh_str);

  RTLibrary rt_lib;
  if (rt_str == "EMBREE")
    rt_lib = RTLibrary::EMBREE;
  else if (rt_str == "GPRT")
    rt_lib = RTLibrary::GPRT;
  else
    fatal_error("Invalid ray tracing library '{}' specified", rt_str);

  // create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();
  mm->parse_metadata();

  auto rti = xdg->ray_tracing_interface();
  rti->init();

  if (args.get<bool>("--list")) {
    std::cout << "Volumes: " << std::endl;
    for (auto volume : mm->volumes()) {
      std::cout << volume << std::endl;
    }
    exit(0);
  }

  MeshID volume = args.get<int>("volume");
  xdg->prepare_volume_for_raytracing(volume);

  Position origin = args.get<std::vector<double>>("--origin");
  Direction direction = args.get<std::vector<double>>("--direction");
  direction.normalize();

  std::cout << "Origin: " << origin[0] << ", " << origin[1] << ", " << origin[2] << std::endl;
  std::cout << "Direction: " << direction[0] << ", " << direction[1] << ", " << direction[2] << std::endl;

  auto result = xdg->ray_fire(volume, origin, direction);

  std::cout << "Distance: " << result.first << std::endl;
  std::cout << "Surface: " << result.second << std::endl;

  return 0;
}
