#include <iostream>
#include <iomanip>
#include <random>

#include "xdg/xdg.h"
#include "xdg/mesh_managers.h"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mesh_file>\n";
        return 1;
    }
    const std::string mesh_file = argv[1];

    std::shared_ptr<xdg::XDG> xdg = xdg::XDG::create();
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(mesh_file);
    mesh_manager->init();
    xdg->prepare_raytracer();

    xdg::BoundingBox bbox = mesh_manager->global_bounding_box();
    double bbox_volume = (bbox.max_x - bbox.min_x)
                         * (bbox.max_y - bbox.min_y)
                         * (bbox.max_z - bbox.min_z);

    std::mt19937 gen(42);
    std::uniform_real_distribution<double> x_dist(bbox.min_x, bbox.max_x);
    std::uniform_real_distribution<double> y_dist(bbox.min_y, bbox.max_y);
    std::uniform_real_distribution<double> z_dist(bbox.min_z, bbox.max_z);

    constexpr size_t n_samples = 100000;

    std::cout << std::setw(10) << "Volume ID"
              << std::setw(20) << "MC Volume Estimate"
              << std::setw(20) << "Hits / Samples\n";
    std::cout << std::string(50, '-') << "\n";

    for (xdg::MeshID volume : mesh_manager->volumes()) {
        size_t hits = 0;
        for (size_t i = 0; i < n_samples; ++i) {
            xdg::Position sample = {x_dist(gen), y_dist(gen), z_dist(gen)};
            if (xdg->point_in_volume(volume, sample))
                hits++;
        }
        const double mc_volume = bbox_volume * static_cast<double>(hits) / n_samples;
        std::cout << std::setw(10) << volume
                  << std::setw(20) << std::fixed << std::setprecision(4) << mc_volume
                  << std::setw(14) << hits << " / " << n_samples << "\n";
    }

    return 0;
}