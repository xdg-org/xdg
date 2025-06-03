// stl includes
#include <iostream>
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/xdg.h"

#include "particle_sim.h"

using namespace xdg;

class CrossCheck {

public:
    CrossCheck(std::vector<std::pair<std::string, MeshLibrary>> test_cases) : test_cases_(test_cases) {}

    // Methods
    void transport() {
      for (const auto& test_case : test_cases_) {
        std::shared_ptr<XDG> xdg {XDG::create(test_case.second)};
        xdg->mesh_manager()->load_file(test_case.first);
        xdg->mesh_manager()->init();
        xdg->mesh_manager()->parse_metadata();
        xdg->prepare_raytracer();

        SimulationData sim_data;

        sim_data.xdg_ = xdg;
        sim_data.verbose_particles_ = false;
        sim_data.implicit_complement_is_graveyard_ = true;

        transport_particles(sim_data);
        sim_data_.push_back(sim_data);
      }
    }

    void check() {
      auto ref_data_ = sim_data_[0];
      for(int i = 1; i < sim_data_.size(); i++) {
        auto data = sim_data_[i];
        for (const auto& [volume, distance] : ref_data_.cell_tracks) {
          REQUIRE_THAT(data.cell_tracks[volume], Catch::Matchers::WithinAbs(ref_data_.cell_tracks[volume], 1e-10));
        }
      }

    }

private:
  // Data members
  std::vector<SimulationData> sim_data_;
  //! A set of test cases (pairs of filenames and mesh libraries) to compare
  std::vector<std::pair<std::string, MeshLibrary>> test_cases_;
};

TEST_CASE("Test MOAB-libMesh Cross-Check 1 Vol")
{
  auto harness = CrossCheck({{"jezebel.exo", MeshLibrary::LIBMESH}, {"jezebel.h5m", MeshLibrary::MOAB}});
  harness.transport();
  harness.check();
}

TEST_CASE("Test MOAB-libMesh Cross-Check 2 Vol")
{
  auto harness = CrossCheck({{"cyl-brick.exo", MeshLibrary::LIBMESH}, {"cyl-brick.h5m", MeshLibrary::MOAB}});
  harness.transport();
  harness.check();
}

TEST_CASE("Test MOAB-libMesh Cross-Check Pincell -- Implicit libMesh Boundaries")
{
  auto harness = CrossCheck({{"pincell-implicit.exo", MeshLibrary::LIBMESH}, {"pincell.h5m", MeshLibrary::MOAB}});
  harness.transport();
  harness.check();
}