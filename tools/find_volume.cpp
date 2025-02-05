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

  argparse::ArgumentParser args("XDG Find Volume Tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
    .help("Path to the input file");

  args.add_argument("-l", "--list")
    .default_value(false)
    .implicit_value(true)
    .help("List all volumes in the file and exit");

  args.add_argument("-p",  "--position")
    .default_value(std::vector<double>{0.0, 0.0, 0.0})
    .help("Ray origin").scan<'g', double>().nargs(3);

  args.add_argument("-d", "--direction")
    .default_value(std::vector<double>{0.0, 0.0, 1.0})
    .help("Ray direction").scan<'g', double>().nargs(3);

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    exit(0);
  }

  // create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();
  xdg->prepare_raytracer();

  if (args.get<bool>("--list")) {
    std::cout << "Volumes: " << std::endl;
    for (auto volume : mm->volumes()) {
      std::cout << volume << std::endl;
    }
    exit(0);
  }

  Position position = args.get<std::vector<double>>("--position");
  Direction direction = args.get<std::vector<double>>("--direction");

  MeshID volume = xdg->find_volume(position, direction);

  std::cout << "Find Volume" << std::endl;
  std::cout << "-----------" << std::endl;
  if (volume == ID_NONE) {
    std::cout << "No volume found for position " << position << std::endl;
  } else {
    std::cout << "Point " << position << " is in Volume " << volume << std::endl;
  }
  std::cout << "-----------" << std::endl;

  // perform the same check using a point in volume loop
  MeshID piv_volume = ID_NONE;
  for (auto volume_id : mm->volumes()) {
    if (xdg->point_in_volume(volume_id, position, &direction)) {
      piv_volume = volume_id;
      break;
    }
  }

  std::cout << "Point in Volume" << std::endl;
  std::cout << "---------------" << std::endl;
  if (piv_volume == ID_NONE) {
    std::cout << "No volume found for position " << position << std::endl;
  } else {
    std::cout << "Point " << position << " is in Volume " << piv_volume << std::endl;
  }
  std::cout << "---------------" << std::endl;

  return 0;
}