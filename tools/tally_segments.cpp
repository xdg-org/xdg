#include <iostream>
#include <memory>
#include <string>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

#include "argparse/argparse.hpp"

#include "tally_segments.h"

using namespace xdg;

int main(int argc, char** argv) {

// argument parsing
argparse::ArgumentParser args("XDG Mesh Tally Simulator", "1.0", argparse::default_arguments::help);

args.add_argument("filename")
    .help("Path to the input file");

args.add_argument("-l", "--library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

args.add_argument("-n", "--num-tracks")
    .help("Number of tracks to simulate")
    .default_value(1000)
    .scan<'i', int>();

args.add_argument("-t", "--threads")
    .help("Number of threads to use")
    .default_value(-1)
    .scan<'i', int>();

args.add_argument("-v", "--verbose")
    .default_value(false)
    .implicit_value(true)
    .help("Enable verbose output");

args.add_argument("-q", "--quiet")
    .default_value(false)
    .implicit_value(true)
    .help("Minimize all output (for performance testing)");

args.add_argument("-c", "--check-tracks")
    .help("Verify that track lengths always match the sum of segments")
    .flag();

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
xdg->prepare_raytracer();

// get the bounding box of the mesh
BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << std::endl;

bool check_tracks = args.get<bool>("--check-tracks");

size_t n_tracks = args.get<int>("--num-tracks");

TallyContext tally_context;
tally_context.xdg_ = xdg;
int n_threads = args.get<int>("--threads");
if (n_threads >= 1) tally_context.n_threads_ = n_threads;
tally_context.n_tracks_ = args.get<int>("--num-tracks");
tally_context.check_tracks_ = args.get<bool>("--check-tracks");
tally_context.verbose_ = args.get<bool>("--verbose");
tally_context.quiet_ = args.get<bool>("--quiet");

tally_segments(tally_context);

return 0;

}
