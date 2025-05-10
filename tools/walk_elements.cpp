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


Position sample_box_location(const BoundingBox& bbox) {
  return bbox.lower_left() + bbox.width() * Vec3da(drand48(), drand48(), drand48());
}

int main(int argc, char** argv) {

// argument parsing
argparse::ArgumentParser args("XDG Particle Pseudo-Simulation", "1.0", argparse::default_arguments::help);

args.add_argument("filename")
    .help("Path to the input file");

args.add_argument("-l", "--library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

args.add_argument("-n", "--num-tracks")
    .help("Number of tracks to simulate")
    .default_value(1000)
    .scan<'i', int>();


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

// get the bounding box of the mesh
BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << std::endl;

size_t n_tracks = args.get<int>("--num-tracks");

for (int i = 0; i < n_tracks; i++) {
  // sample a location within the bounding box
  Position r1 = sample_box_location(bbox);
  if (!bbox.contains(r1)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r1));

  Position r2 = sample_box_location(bbox);
  if (!bbox.contains(r2)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r2));

  auto segments = xdg->segments(r1, r2);
  std::cout << fmt::format("Track {}: {} segments", i, segments.size()) << std::endl;
}


return 0;

}