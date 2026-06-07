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


TEST_CASE("Test MOAB-libMesh Cross-Check Tallies -- Simple Cubes, Tet Mesh")
{
  auto xdg_moab = XDG::create(MeshLibrary::MOAB);
  xdg_moab->mesh_manager()->load_file("cube-w-multiblock-sideset.h5m");
  xdg_moab->mesh_manager()->init();
  xdg_moab->mesh_manager()->parse_metadata();
  xdg_moab->prepare_raytracer();

  auto xdg_libmesh = XDG::create(MeshLibrary::LIBMESH);
  xdg_libmesh->mesh_manager()->load_file("cube-w-multiblock-sideset.exo");
  xdg_libmesh->mesh_manager()->init();
  xdg_libmesh->mesh_manager()->parse_metadata();
  xdg_libmesh->prepare_raytracer();

  // check that the global bounding box of the model and various model counts are the same
  REQUIRE(xdg_moab->mesh_manager()->num_vertices() == xdg_libmesh->mesh_manager()->num_vertices());
  REQUIRE(xdg_moab->mesh_manager()->num_volume_elements() == xdg_libmesh->mesh_manager()->num_volume_elements());
  REQUIRE(xdg_moab->mesh_manager()->num_volumes() == xdg_libmesh->mesh_manager()->num_volumes());
  auto moab_bounding_box = xdg_moab->mesh_manager()->global_bounding_box();
  auto libmesh_bounding_box = xdg_libmesh->mesh_manager()->global_bounding_box();
  REQUIRE_THAT(moab_bounding_box.min_x, Catch::Matchers::WithinAbs(libmesh_bounding_box.min_x, 1e-6));
  REQUIRE_THAT(moab_bounding_box.min_y, Catch::Matchers::WithinAbs(libmesh_bounding_box.min_y, 1e-6));
  REQUIRE_THAT(moab_bounding_box.min_z, Catch::Matchers::WithinAbs(libmesh_bounding_box.min_z, 1e-6));
  REQUIRE_THAT(moab_bounding_box.max_x, Catch::Matchers::WithinAbs(libmesh_bounding_box.max_x, 1e-6));
  REQUIRE_THAT(moab_bounding_box.max_y, Catch::Matchers::WithinAbs(libmesh_bounding_box.max_y, 1e-6));
  REQUIRE_THAT(moab_bounding_box.max_z, Catch::Matchers::WithinAbs(libmesh_bounding_box.max_z, 1e-6));

  // sample start and end locations within the bounding box of these models
  int num_samples = 10000;
  for (int i = 0; i < num_samples; i++) {
    Position start = moab_bounding_box.sample_location();
    Position end = moab_bounding_box.sample_location();

    auto moab_element = xdg_moab->find_element(start);
    auto libmesh_element = xdg_libmesh->find_element(start);

      REQUIRE(moab_element != ID_NONE);
      REQUIRE(libmesh_element != ID_NONE);
      // check element equivalence by index b/c IDs may be different depending on the library conventions
      REQUIRE(xdg_moab->mesh_manager()->element_index(moab_element) == xdg_libmesh->mesh_manager()->element_index(libmesh_element));

    auto moab_tracks = xdg_moab->segments(start, end);
    auto libmesh_tracks = xdg_libmesh->segments(start, end);

    REQUIRE(moab_tracks.size() == libmesh_tracks.size());
    for (size_t j = 0; j < moab_tracks.size(); j++) {
      REQUIRE(xdg_moab->mesh_manager()->element_index(moab_tracks[j].first) == xdg_libmesh->mesh_manager()->element_index(libmesh_tracks[j].first));
      REQUIRE_THAT(moab_tracks[j].second, Catch::Matchers::WithinAbs(libmesh_tracks[j].second, 1e-10));
    }
  }
}