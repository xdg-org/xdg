// stl includes
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef XDG_HAVE_OPENMP
#include <omp.h>
#endif

// xdg includes
#include "xdg/error.h"
#include "xdg/overlap.h"

using namespace xdg;

struct OMP_SingleThreadFixture {
    OMP_SingleThreadFixture() {
      #ifdef XDG_HAVE_OPENMP
        omp_set_num_threads(1);
      #endif
    }
};

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Overlapping Volumes Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("overlap.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = false;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected 1 overlap
  REQUIRE(overlap_map.size() == 2);
  std::set<int> expected_overlaps = {1, 2};

  // Expected overlaps between volumes [1,2]
  REQUIRE(expected_overlaps == overlap_map.begin()->first);
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Non-Overlapping Volumes Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("no_overlap.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = false;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected no overlaps
  REQUIRE(overlap_map.size() == 0);
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Non-Overlapping Imprinted Volumes Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("no_overlap_imp.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = false;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected no overlaps
  REQUIRE(overlap_map.size() == 0);
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Enclosed Volume Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("enclosed.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = false;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected 1 overlap
  REQUIRE(overlap_map.size() == 2);
  std::set<int> expected_overlaps = {1, 2};

  // Expected overlaps between volumes [1,2]
  REQUIRE(expected_overlaps == overlap_map.begin()->first);
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Small Overlap Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("small_overlap.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = false;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected 1 overlap
  REQUIRE(overlap_map.size() == 1);
  std::set<int> expected_overlaps = {1, 2};

  // Expected overlaps between volumes [1,2]
  REQUIRE(expected_overlaps == overlap_map.begin()->first);
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Small Edge Overlap Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("overlap-edge.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = true;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected 1 overlap
  REQUIRE(overlap_map.size() == 1);
  std::set<int> expected_overlaps = {1, 2};

  for (const auto& [key,value]:overlap_map)
  {
    // Expected overlaps between volumes [1,2]
    REQUIRE(expected_overlaps == key);

    // Expected overlap location
    std::array<double, 3> expected = {0.236122, 0.441025, -0.0832704};

    double tol = 1e-6;
    REQUIRE_THAT(value.x, Catch::Matchers::WithinAbs(expected[0], tol));
    REQUIRE_THAT(value.y, Catch::Matchers::WithinAbs(expected[1], tol));
    REQUIRE_THAT(value.z, Catch::Matchers::WithinAbs(expected[2], tol));
  }
}

TEST_CASE_METHOD(OMP_SingleThreadFixture, "Beam Edge Overlap Test", "[single-thread]")
{
  // Create a mesh manager
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("beam-overlaps.h5m");
  mm->init();
  xdg->prepare_raytracer();
  OverlapMap overlap_map;
  bool checkEdges = true;
  bool verboseOutput = false;
  check_instance_for_overlaps(xdg, overlap_map, checkEdges, verboseOutput);

  // Expected 1 overlap
  REQUIRE(overlap_map.size() == 1);
  std::set<int> expected_overlaps = {1, 2};

  for (const auto& [key,value]:overlap_map)
  {
    // Expected overlaps between volumes [1,2]
    REQUIRE(expected_overlaps == key);

    // Expected overlap location
    std::array<double, 3> expected = {-1.5, 0.75, -0.5};

    double tol = 1e-6;
    REQUIRE_THAT(value.x, Catch::Matchers::WithinAbs(expected[0], tol));
    REQUIRE_THAT(value.y, Catch::Matchers::WithinAbs(expected[1], tol));
    REQUIRE_THAT(value.z, Catch::Matchers::WithinAbs(expected[2], tol));
  }
}