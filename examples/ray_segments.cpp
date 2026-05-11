#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>

#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"
#include "argparse/argparse.hpp"
#include "xdg/error.h"

int main(int argc, char* argv[])
{

    argparse::ArgumentParser args("An example of how to setup XDG's C++ API  to calculate "
                                  "Ray segments in a mesh ", "1.0", argparse::default_arguments::help);

    args.add_argument("-f","filename").help("Path to the mesh file");
    args.add_argument("-m", "--mesh-library").help("Mesh library to use. One of (MOAB, LIBMESH)").default_value("MOAB");
    args.add_argument("-r", "--rt-library").help("Ray tracing library to use. One of (EMBREE, GPRT)").default_value("EMBREE");

    try {
        args.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << args;
        exit(0);
    }


    const std::string mesh_lib_name = args.get<std::string>("--mesh-library");
    const std::string rt_lib_name = args.get<std::string>("--rt-library");
    const std::string mesh_file = args.get<std::string>("filename");

    xdg::RTLibrary rt_lib;
    if (rt_lib_name == "EMBREE")
        rt_lib = RTLibrary::EMBREE;
    else if (rt_lib_name == "GPRT")
        rt_lib = RTLibrary::GPRT;
    else
        xdg::fatal_error("Invalid ray tracing library '{}' specified", rt_str);

    xdg::MeshLibrary mesh_lib;
    if (mesh_lib_name == "MOAB")
        mesh_lib = MeshLibrary::MOAB;
    else if (mesh_lib_name == "LIBMESH") {
        mesh_lib = MeshLibrary::LIBMESH;
        if (rt_lib == RTLibrary::GPRT)
            xdg::fatal_error("LibMesh is not currently supported with GPRT");
    }
    else
        xdg::fatal_error("Invalid mesh library '{}' specified", mesh_lib_name);


    std::shared_ptr<XDG> xdg = XDG::create(mesh_lib, rt_lib );
    const auto& mesh_manager = xdg->mesh_manager();

    mesh_manager->load_file(mesh_file);
    mesh_manager->init();

    xdg->prepare_raytracer();

    xdg::BoundingBox bbox = mesh_manager->global_bounding_box();

    std::mt19937 gen(42);
    std::uniform_real_distribution<double> x_dist(bbox.min_x, bbox.max_x);
    std::uniform_real_distribution<double> y_dist(bbox.min_y, bbox.max_y);
    std::uniform_real_distribution<double> z_dist(bbox.min_z, bbox.max_z);

    constexpr size_t number_of_rays = 10000;

    for (size_t i = 0; i < number_of_rays; ++i)
    {
        xdg::Position start = {x_dist(gen), y_dist(gen), z_dist(gen)};
        xdg::Position end   = {x_dist(gen), y_dist(gen), z_dist(gen)};

        // xdg returns MeshID (or the element ID) of the elements it has intersected and
        // ray segments associated with element.
        std::vector<std::pair<xdg::MeshID, double>> track_segments = xdg_instance->segments(start, end);

        double total_length = std::accumulate(track_segments.begin(), track_segments.end(), 0.0,
                                              [](double sum, const std::pair<xdg::MeshID, double>& seg) {
                                                  return sum + seg.second;
                                              });

        double ref_distance = (end - start).length();

        double error_pct = (ref_distance > 0.0)
                           ? (total_length - ref_distance) * 100.0 / ref_distance
                           : 0.0;

        std::cout
                << "Elements hit = " << std::setw(4)  << track_segments.size()
                << "  ref dist = "   << std::fixed     << std::setprecision(6) << ref_distance
                << "  xdg dist = "                     << total_length
                << "  error = "      << std::setprecision(4) << error_pct << " %\n";
    }

    return 0;
}