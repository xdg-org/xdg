#include "xdg/xdg.h"
#include "xdg/error.h"

#include "argparse/argparse.hpp"

using namespace xdg;

int main(int argc, char** argv) {
  // argument parsing
  argparse::ArgumentParser args("XDG Model Info", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
      .help("Path to the input file");

  args.add_argument("-v", "--verbose")
      .default_value(false)
      .implicit_value(true)
      .help("Enable verbose output of particle events");

  args.add_argument("-l", "--library")
      .help("Mesh library to use. One of (MOAB, LIBMESH)")
      .default_value("MOAB");

      args.add_argument("-x", "--xml")
          .help("Path to the output XML file for the model representation")
          .default_value(std::string{});

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


  // Load the mesh
  std::string filename = args.get<std::string>("filename");
  xdg->mesh_manager()->load_file(filename);
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();

  // Print the mesh information
  xdg->mesh_manager()->display_model_topology();

  // Write the mesh information to an XML file
  std::string xml_filename = args.get<std::string>("--xml");
  if (!xml_filename.empty()) {
    xdg->mesh_manager()->write_topology_to_xml(xml_filename);
  }

  return 0;
}