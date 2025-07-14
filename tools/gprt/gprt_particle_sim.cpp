#include <iostream>
#include <memory>
#include <string>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/gprt/ray_tracer.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"
#include "argparse/argparse.hpp"

#include "gprt_particle_sim.h"

using namespace xdg;

int main(int argc, char** argv) {

// argument parsing
argparse::ArgumentParser args("XDG Particle Pseudo-Simulation with GPRT", "1.0", argparse::default_arguments::help);

args.add_argument("filename")
    .help("Path to the input file");

args.add_argument("-v", "--verbose")
    .default_value(false)
    .implicit_value(true)
    .help("Enable verbose output of particle events");

args.add_argument("-m", "--mfp")
    .default_value(1.0)
    .help("Mean free path of the particles")
    .scan<'g', double>();

args.add_argument("--mesh-library")
    .help("Mesh library to use. One of (MOAB, LIBMESH)")
    .default_value("MOAB");

args.add_argument("--rt-library")
    .help("Ray tracing library to use. One of (EMBREE, GPRT)")
    .default_value("GPRT");

try {
  args.parse_args(argc, argv);
}
catch (const std::runtime_error& err) {
  std::cout << err.what() << std::endl;
  std::cout << args;
  exit(1);
}

// Problem Setup
srand48(42);

SimulationData sim_data;

// create a xdg instance
std::shared_ptr<XDG> xdg {nullptr};

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
  fatal_error("Expecting GPRT for now");
  // rt_lib = RTLibrary::EMBREE;
else if (rt_str == "GPRT")
  rt_lib = RTLibrary::GPRT;
else
  fatal_error("Invalid ray tracing library '{}' specified", rt_str);

// Create the XDG instance with specified libraries
xdg = XDG::create(mesh_lib, rt_lib);

sim_data.xdg_ = xdg;

const auto& mm = xdg->mesh_manager();
mm->load_file(args.get<std::string>("filename"));
mm->init();
// mm->parse_metadata();
xdg->prepare_raytracer();

auto rti = xdg->ray_tracing_interface();
rti->init();

bool renderMesh = false;
// Create a GPRTRayTracer pointer to access render methods
if (renderMesh)
{
  auto gprt_rti = std::dynamic_pointer_cast<GPRTRayTracer>(rti);
  gprt_rti->render_mesh(mm);
}

// update the mean free path
sim_data.mfp_ = args.get<double>("--mfp");

sim_data.verbose_particles_ = args.get<bool>("--verbose");

transport_particles(sim_data);

// report distances in each cell in a table
write_message("Cell Track Lengths");
write_message("-----------");
for (const auto& [cell, dist] : sim_data.cell_tracks) {
  write_message("Cell {}: {}", cell, dist);
}
write_message("-----------");


return 0;
}
