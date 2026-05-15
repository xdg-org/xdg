#include <iostream>
#include <memory>
#include <string>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/mesh_managers.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

#include "particle_sim.h"

using namespace xdg;

int main(int argc, char** argv) {

  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MfemMeshManager>();
  argparse::ArgumentParser args("MFEM debugging tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename").help("Path to the input file");

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    exit(0);
  }

  mesh_manager->load_file(args.get<std::string>("filename"));
  mesh_manager->init();
  
}
