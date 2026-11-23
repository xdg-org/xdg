// stl includes
#include <cassert>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// libMesh includes
#include "libmesh/mesh_refinement.h"
#include "libmesh/elem.h"
#include "libmesh/mesh.h"

// xdg includes
#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "xdg/embree/ray_tracer.h"

using namespace xdg;

struct TetAMRMesh {
  static constexpr std::string_view filename = "amr_mesh_tet.exo";
  static constexpr int num_elements_total = 6352; 
  static constexpr int num_faces_vol1 = 768; 
  static constexpr int num_faces_ipc = 1103; 
  // the volume of an element sampled at (0.25, 0.25, 0.25)
  static constexpr double h0_elem_volume = 0.020833333; 
  // the volume of an element sampled at (0.125, 0.125, 0.875)
  static constexpr double h1_elem_volume = h0_elem_volume / 8.0;
  // the volume of an element sampled at (0.0625, 0.0625, 1.0625)
  static constexpr double h2_elem_volume = h1_elem_volume / 8.0;
  // valid segments along a line from (1.9375, 0.0625, -1.0) to (1.9375, 0.0625, 3.0)
  // this contains elements at different refinement states
  static constexpr std::array<double, 22> valid_segments_1 {
    0.0625, 0.375,  0.0625, 0.0625, 0.375,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625
  };
  // valid segments along a line from (0.0625, 0.0625, -1.0) to (0.0625, 0.0625, 3.0)
  // this crosses an interface between volumes, which also has different refinement state elements
  static constexpr std::array<double, 25> valid_segments_2 {
    0.0625, 0.0625, 0.375,  0.0625, 0.0625,
    0.125,  0.0625, 0.0625, 0.125,  0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625
  };
  // valid segments along a line from (-1.0, 0.0625, 1.0625) to (3.0, 0.0625, 1.0625)
  // this crosses a contiguous volume of elements at the same refinement state
  static constexpr std::array<double, 32> valid_segments_3 {
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
    0.0625, 0.0625
  };
};
struct HexAMRMesh {
  static constexpr std::string_view filename = "amr_mesh_hex.exo";
  static constexpr int num_elements_total = 1096;
  static constexpr int num_faces_vol1 = 384;
  static constexpr int num_faces_ipc = 580;
  // the volume of an element sampled at (0.25, 0.25, 0.25)
  static constexpr double h0_elem_volume = 0.125;
  // the volume of an element sampled at (0.125, 0.125, 0.875)
  static constexpr double h1_elem_volume = h0_elem_volume / 8.0;
  // the volume of an element sampled at (0.0625, 0.0625, 1.0625)
  static constexpr double h2_elem_volume = h1_elem_volume / 8.0;
  // valid segments along a line from (1.9375, 0.0625, -1.0) to (1.9375, 0.0625, 3.0)
  // this contains elements at different refinement states
  static constexpr std::array<double, 11> valid_segments_1 {
    0.5,   0.25,  0.25,  0.125, 0.125,
    0.125, 0.125, 0.125, 0.125, 0.125,
    0.125
  };
  // valid segments along a line from (0.0625, 0.0625, -1.0) to (0.0625, 0.0625, 3.0)
  // this crosses an interface between volumes, which also has different refinement state elements
  static constexpr auto valid_segments_2 = valid_segments_1;
  // valid segments along a line from (-1.0, 0.0625, 1.0625) to (3.0, 0.0625, 1.0625)
  // this crosses a contiguous volume of elements at the same refinement state
  static constexpr std::array<double, 16> valid_segments_3 {
    0.125, 0.125, 0.125, 0.125, 0.125,
    0.125, 0.125, 0.125, 0.125, 0.125,
    0.125, 0.125, 0.125, 0.125, 0.125,
    0.125
  };
};

TEMPLATE_TEST_CASE("Test libMesh AMR Mesh", "[libmesh][amr]",
                   TetAMRMesh,
                   HexAMRMesh)
{
  using AMRMesh = TestType;
  std::string filename {AMRMesh::filename};

  const libMesh::Point bottom_left = {0.0, 0.0, 1.0};
  const libMesh::Point top_right = {2.0, 1.0, 2.0};

  DYNAMIC_SECTION("Mesh: " << filename) {
    auto mesh = std::make_unique<libMesh::Mesh>(*XDGConfig::config().libmesh_comm(), 3);
    mesh->read(filename);

    // mesh->allow_renumbering(false) prevents libMesh from shuffling elements to different locations in
    // the element array and changing element IDs (since element IDs in libMesh are 
    // indices into the element array). This creates holes in the ID space when elements are 
    // deleted (such as when mesh->contract() is called) to test the index -> ID mapping. 
    mesh->allow_renumbering(false);
    libMesh::MeshRefinement mesh_refinement((*mesh.get()));

    // Apply AMR to the mesh two times. There doesn't appear to be a way to store
    // AMR connectivity in Exodus output, so we need to generate it here
    // A single round of refinement followed by coarsening (when the mesh isn't renumbering)
    // creates holes in the libMesh element array (and therefore the id space). This allows us
    // to test the index->element (and dual) mapping under AMR.
    const std::array<libMesh::Elem::RefinementState, 4> amr_state {
      libMesh::Elem::RefinementState::REFINE,
      libMesh::Elem::RefinementState::COARSEN,
      libMesh::Elem::RefinementState::REFINE,
      libMesh::Elem::RefinementState::REFINE
    };
    for (int i = 0; i < 4; ++i) {
      for (libMesh::Elem * elem : mesh->active_element_ptr_range()) {
        const auto & va = elem->vertex_average();
        bool refine = true;
        for (int coord = 0; coord < 3; ++coord) {
          refine &= va(coord) >= bottom_left(coord) && va(coord) < top_right(coord);
        }
        if (refine) {
          elem->set_refinement_flag(amr_state[i]);
        }
      }

      mesh_refinement.refine_and_coarsen_elements();
      mesh_refinement.clean_refinement_flags();
      // Contract deletes more refined child elements.
      mesh->contract();
    }
    mesh->prepare_for_use();

    auto mesh_manager = std::make_shared<xdg::LibMeshManager>(mesh.get());
    REQUIRE(mesh_manager->mesh_library() == MeshLibrary::LIBMESH);
    mesh_manager->init();
    mesh_manager->parse_metadata();
    auto xdg = std::make_shared<xdg::XDG>(mesh_manager);
    xdg->prepare_raytracer();

    REQUIRE(mesh_manager->num_volumes() == 3);
    REQUIRE(mesh_manager->num_volume_elements() == AMRMesh::num_elements_total);
    // This will fail if the one-to-many mapping for interface faces is wrong.
    REQUIRE(mesh_manager->num_volume_faces(1) == AMRMesh::num_faces_vol1);
    // This will fail if the boundary surfaces contain lower refinement state elements.
    REQUIRE(mesh_manager->num_volume_faces(mesh_manager->implicit_complement()) == AMRMesh::num_faces_ipc);

    // Test element ID to index mapping.
    {
      int expected_index = 0;
      for (const auto* elem : mesh->active_element_ptr_range()) {
        REQUIRE(mesh_manager->element_index(elem->id()) == expected_index);
        expected_index++;
      }
    }

    // Test element index to ID mappings.
    {
      std::vector<MeshID> elem_ids;
      for (const auto* elem : mesh->active_element_ptr_range()) {
        elem_ids.push_back(elem->id());
      }
      for (int elem_idx = 0; elem_idx < mesh_manager->num_volume_elements(); ++elem_idx) {
        REQUIRE(mesh_manager->element_id(elem_idx) == elem_ids[elem_idx]);
      }
    }

    // Testing find element calls for different refinement levels.
    {
      auto elem1 = xdg->find_element({0.0625, 0.0625, 1.0625});
      REQUIRE(elem1 != xdg::ID_NONE);
      REQUIRE_THAT(mesh_manager->element_volume(elem1), Catch::Matchers::WithinAbs(AMRMesh::h2_elem_volume, 1e-6));
      auto elem2 = xdg->find_element({0.125, 0.125, 0.875});
      REQUIRE(elem2 != xdg::ID_NONE);
      REQUIRE_THAT(mesh_manager->element_volume(elem2), Catch::Matchers::WithinAbs(AMRMesh::h1_elem_volume, 1e-6));
      auto elem3 = xdg->find_element({0.25, 0.25, 0.25});
      REQUIRE(elem3 != xdg::ID_NONE);
      REQUIRE_THAT(mesh_manager->element_volume(elem3), Catch::Matchers::WithinAbs(AMRMesh::h0_elem_volume, 1e-6));
    }

    // Test ray fire on an interface with AMR connectivity.
    {
      auto enter_vol1 = xdg->ray_fire(1, {0.0625, 0.0625, -1.0}, {0.0, 0.0, 1.0}, xdg::INFTY, xdg::HitOrientation::ENTERING);
      REQUIRE_THAT(enter_vol1.first, Catch::Matchers::WithinAbs(2.0, 1e-6));
      REQUIRE(enter_vol1.second == 6);
      auto exit_vol1 = xdg->ray_fire(1, {0.0625, 0.0625, -1.0}, {0.0, 0.0, 1.0}, xdg::INFTY, xdg::HitOrientation::EXITING);
      REQUIRE_THAT(exit_vol1.first, Catch::Matchers::WithinAbs(3.0, 1e-6));
      REQUIRE(exit_vol1.second == 7);

      enter_vol1 = xdg->ray_fire(1, {0.0625, 0.0625, 3.0}, {0.0, 0.0, -1.0}, xdg::INFTY, xdg::HitOrientation::EXITING);
      REQUIRE_THAT(enter_vol1.first, Catch::Matchers::WithinAbs(2.0, 1e-6));
      REQUIRE(enter_vol1.second == 6);
      exit_vol1 = xdg->ray_fire(1, {0.0625, 0.0625, 3.0}, {0.0, 0.0, -1.0}, xdg::INFTY, xdg::HitOrientation::ENTERING);
      REQUIRE_THAT(exit_vol1.first, Catch::Matchers::WithinAbs(1.0, 1e-6));
      REQUIRE(exit_vol1.second == 7);
    }

    auto zero_dist = [](const std::pair<xdg::MeshID, double>& p) { return p.second == 0.0; };

    // Test crossing a contiguous AMR volume with segments.
    {
      auto results_fw_1 = xdg->segments({1.9375, 0.0625, -1.0}, {1.9375, 0.0625, 3.0});
      results_fw_1.erase(std::remove_if(results_fw_1.begin(), results_fw_1.end(), zero_dist), results_fw_1.end());

      REQUIRE(results_fw_1.size() == AMRMesh::valid_segments_1.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_1.size(); ++i) {
        REQUIRE_THAT(results_fw_1[i].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_1[i], 1e-6));
      }

      auto results_bw_1 = xdg->segments({1.9375, 0.0625, 3.0}, {1.9375, 0.0625, -1.0});
      results_bw_1.erase(std::remove_if(results_bw_1.begin(), results_bw_1.end(), zero_dist), results_bw_1.end());

      REQUIRE(results_bw_1.size() == AMRMesh::valid_segments_1.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_1.size(); ++i) {
        REQUIRE_THAT(results_bw_1[AMRMesh::valid_segments_1.size() - i - 1].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_1[i], 1e-6));
      }
    }

    // Test crossing an AMR interface boundary with segments.
    {
      auto results_fw_2 = xdg->segments({0.0625, 0.0625, -1.0}, {0.0625, 0.0625, 3.0});
      results_fw_2.erase(std::remove_if(results_fw_2.begin(), results_fw_2.end(), zero_dist), results_fw_2.end());

      REQUIRE(results_fw_2.size() == AMRMesh::valid_segments_2.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_2.size(); ++i) {
        REQUIRE_THAT(results_fw_2[i].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_2[i], 1e-6));
      }
      auto results_bw_2 = xdg->segments({0.0625, 0.0625, 3.0}, {0.0625, 0.0625, -1.0});
      results_bw_2.erase(std::remove_if(results_bw_2.begin(), results_bw_2.end(), zero_dist), results_bw_2.end());

      REQUIRE(results_bw_2.size() == AMRMesh::valid_segments_2.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_2.size(); ++i) {
        REQUIRE_THAT(results_bw_2[AMRMesh::valid_segments_2.size() - i - 1].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_2[i], 1e-6));
      }
    }

    // Test crossing an interface boundary where elements are all at the same refinement state.
    {
      auto results_fw_3 = xdg->segments({-1.0, 0.0625, 1.0625}, {3.0, 0.0625, 1.0625});
      results_fw_3.erase(std::remove_if(results_fw_3.begin(), results_fw_3.end(), zero_dist), results_fw_3.end());

      REQUIRE(results_fw_3.size() == AMRMesh::valid_segments_3.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_3.size(); ++i) {
        REQUIRE_THAT(results_fw_3[i].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_3[i], 1e-6));
      }
      auto results_bw_3 = xdg->segments({3.0, 0.0625, 1.0625}, {-1.0, 0.0625, 1.0625});
      results_bw_3.erase(std::remove_if(results_bw_3.begin(), results_bw_3.end(), zero_dist), results_bw_3.end());

      REQUIRE(results_bw_3.size() == AMRMesh::valid_segments_3.size());
      for (std::size_t i = 0; i < AMRMesh::valid_segments_3.size(); ++i) {
        REQUIRE_THAT(results_bw_3[AMRMesh::valid_segments_3.size() - i - 1].second, Catch::Matchers::WithinAbs(AMRMesh::valid_segments_3[i], 1e-6));
      }
    }
  } // DYNAMIC_SECTION
}