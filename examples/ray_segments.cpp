#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>

#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"

int main(int argc, char* argv[])
{
    // Not sure if this is the best way but based on CI matrix in the event
    // where MOAB and LibMesh both defined we are gonna end up using MOAB mesh manager
#if defined(XDG_ENABLE_MOAB)
    auto mesh_manager = std::make_shared<xdg::MOABMeshManager>();
    mesh_manager->load_file("cube.h5m"); // Provide the path to your mesh file.
#elif defined(XDG_ENABLE_LIBMESH)
    auto mesh_manager = std::make_shared<xdg::LibMeshManager>();
    mesh_manager->load_file("brick.exo"); // Provide the path to your mesh file.
#endif

    mesh_manager->init();

    // by default, we are using Embree as a ray tracer
    auto xdg_instance = std::make_shared<xdg::XDG>(mesh_manager);
    xdg_instance->prepare_raytracer();

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