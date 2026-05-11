#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>

#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"

int main(int argc, char* argv[])
{
// Read in your mesh file  
// Do whatever logic you need to decide whether you want to use xdg with MOAB or libmesh 
// Use xdg factory method XDG::create() - if no libraries specified it defaults to MOAB + embree
if (use_moab) {
   std::shared_ptr<XDG> xdg = XDG::create(xdg::MeshLibrary::MOAB, xdg::RTLibrary::EMBREE);
else {
   std::shared_ptr<XDG> xdg = XDG::create(xdg::MeshLibrary::LIBMESH, xdg::RTLibrary::EMBREE);
}

// Then we can recover the mesh manager object abstractly without needing to specify the derived mesh manager classes (and thus removing the need for the pre-compile guards)
const auto& mesh_manager = xdg->mesh_manager();
mesh_manager->load_file(filename);
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