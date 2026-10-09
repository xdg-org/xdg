// stl includes
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/xdg.h"

#include "particle_sim.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test;

struct MeshCaseInput {
  MeshLibrary mesh_library;
  std::string filename;
};

std::vector<XDGBackendFixture> make_mesh_lib_cases(
  const std::vector<MeshCaseInput>& inputs,
  RTLibrary rt_library = RTLibrary::EMBREE)
{
  std::vector<XDGBackendFixture> mesh_lib_cases;
  for (const auto& input : inputs) {
    if (input.mesh_library == MeshLibrary::MOCK ||
        !mesh_library_available(input.mesh_library)) {
      continue;
    }

    mesh_lib_cases.push_back(
      make_xdg_backend_fixture(input.mesh_library, rt_library, input.filename));
  }

  return mesh_lib_cases;
}

class CrossCheck {

public:
    CrossCheck(const std::vector<XDGBackendFixture>& test_fixtures) : test_fixtures_(test_fixtures) {}

    // Methods
    void transport() {
      for (const auto& test_fixture : test_fixtures_) {
        SimulationData sim_data;

        sim_data.xdg_ = test_fixture.xdg;
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
        CAPTURE(test_fixtures_[0].label(), test_fixtures_[i].label());
        for (const auto& [volume, distance] : ref_data_.cell_tracks) {
          REQUIRE_THAT(data.cell_tracks[volume], Catch::Matchers::WithinAbs(ref_data_.cell_tracks[volume], 1e-10));
        }
      }

    }

private:
  // Data members
  std::vector<SimulationData> sim_data_;
  //! Prepared XDG backend fixtures to compare
  std::vector<XDGBackendFixture> test_fixtures_;
};

TEST_CASE("Test Cross Check Transport across mesh Backends Jezebel")
{
  // Attempt to build all three test fixtures, but skip the test if fewer than two are available
  const auto test_fixtures = make_mesh_lib_cases({
    {MeshLibrary::MOAB, "jezebel.h5m"}, // MOAB passed first so it becomes the reference case
    {MeshLibrary::LIBMESH, "jezebel.exo"},
  });
  if (test_fixtures.size() < 2) {
    SKIP("Fewer than two mesh backends are available; skipping cross-check.");
  }

  auto harness = CrossCheck(test_fixtures);
  harness.transport();
  harness.check();
}

TEST_CASE("Test Cross Check Transport across mesh Backends cyl-brick")
{
  // Attempt to build all three test fixtures, but skip the test if fewer than two are available
  const auto test_fixtures = make_mesh_lib_cases({
    {MeshLibrary::MOAB, "cyl-brick.h5m"}, // MOAB passed first so it becomes the reference case
    {MeshLibrary::LIBMESH, "cyl-brick.exo"},
  });
  if (test_fixtures.size() < 2) {
    SKIP("Fewer than two mesh backends are available; skipping cross-check.");
  }

  auto harness = CrossCheck(test_fixtures);
  harness.transport();
  harness.check();
}

TEST_CASE("Test Cross Check Transport across Backends Pincell -- Implicit libMesh Boundaries")
{
  // Attempt to build all supported test fixtures, but skip the test if fewer than two are available
  const auto test_fixtures = make_mesh_lib_cases({
    {MeshLibrary::MOAB, "pincell.h5m"}, // MOAB passed first so it becomes the reference case
    {MeshLibrary::LIBMESH, "pincell-implicit.exo"}
  });
  if (test_fixtures.size() < 2) {
    SKIP("Fewer than two mesh backends are available; skipping cross-check.");
  }

  auto harness = CrossCheck(test_fixtures);
  harness.transport();
  harness.check();
}


TEST_CASE("Test Mesh Backend Cross-Check Tallies -- Simple Cubes, Tet Mesh")
{
  // Attempt to build all supported test fixtures, but skip the test if fewer than two are available
  const auto test_fixtures = make_mesh_lib_cases({
    {MeshLibrary::MOAB, "cube-w-multiblock-sideset.h5m"}, // MOAB passed first so it becomes the reference case
    {MeshLibrary::LIBMESH, "cube-w-multiblock-sideset.exo"}
  });
  if (test_fixtures.size() < 2) {
    SKIP("Fewer than two mesh backends are available; skipping cross-check.");
  }

  const auto& xdg_moab = test_fixtures[0].xdg;
  const auto& xdg_libmesh = test_fixtures[1].xdg;
  CAPTURE(test_fixtures[0].label(), test_fixtures[1].label());

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
