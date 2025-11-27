#include <iostream>
#include <memory>
#include <string>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

#include "walk_elements.h"

using namespace xdg;

int main(int argc, char** argv) {

  // argument parsing
  argparse::ArgumentParser args("XDG Volumetric Element Tracking Simulator", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
      .help("Path to the input file");

  args.add_argument("-l", "--library")
      .help("Mesh library to use. One of (MOAB, LIBMESH)")
      .default_value("MOAB");

  args.add_argument("-n", "--num-particles")
      .help("Number of particles to simulate")
      .default_value(1000)
      .scan<'i', int>();

  args.add_argument("-t", "--threads")
      .help("Number of threads to use")
      .default_value(-1)
      .scan<'i', int>();

  args.add_argument("-v", "--verbose")
      .default_value(false)
      .implicit_value(true)
      .help("Enable verbose output of particle events");

  args.add_argument("-q", "--quiet")
      .default_value(false)
      .implicit_value(true)
      .help("Minimize all output (for performance testing)");

  args.add_argument("-m", "--mfp")
      .default_value(1.0)
      .help("Mean free path of the particles").scan<'g', double>();

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

  WalkElementsContext walkelementscontext;

  walkelementscontext.xdg_ = xdg;
  int n_threads = args.get<int>("--threads");
  if (n_threads >= 1) walkelementscontext.n_threads_ = n_threads;
  walkelementscontext.n_particles_ = args.get<int>("--num-particles");
  walkelementscontext.mean_free_path_ = args.get<double>("--mfp");
  walkelementscontext.verbose_ = args.get<bool>("--verbose");
  walkelementscontext.quiet_ = args.get<bool>("--quiet");

  walk_elements(walkelementscontext);

  return 0;
}