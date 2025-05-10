#include <iostream>
#include <memory>
#include <string>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

#include "particle_sim.h"

using namespace xdg;

int main(int argc, char** argv) {

// argument parsing
argparse::ArgumentParser args("XDG Particle Pseudo-Simulation", "1.0", argparse::default_arguments::help);

args.add_argument("filename")
    .help("Path to the input file");

args.add_argument("-l", "--library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    exit(0);
  }

// Problem Setup
srand48(42);

// create a mesh manager
std::shared_ptr<XDG> xdg {nullptr};
if (args.get<std::string>("--library") == "MOAB")
  xdg = XDG::create(MeshLibrary::MOAB);
else if (args.get<std::string>("--library") == "LIBMESH")
  xdg = XDG::create(MeshLibrary::LIBMESH);
else
  fatal_error("Invalid mesh library {} specified", args.get<std::string>("--library"));

const auto& mm = xdg->mesh_manager();
mm->load_file(args.get<std::string>("filename"));
mm->init();
mm->parse_metadata();
xdg->prepare_raytracer();

return 0;
}