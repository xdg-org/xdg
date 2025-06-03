#include <random>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/xdg.h"
#include "xdg/constants.h"

#include "mesh_mock.h"


TEST_CASE("Test Internal Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 4);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start {0.0, 0.0, 0.0};
  Position end {1.0, 1.0, 1.0};

  auto track_segments = xdg->segments(volume_id, start, end);

  double total_length {0.0};
  for (auto& segment : track_segments) {
    total_length += segment.second;
  }
  REQUIRE(total_length == Catch::Approx((start-end).length()).epsilon(0.00001));
}

TEST_CASE("Test Intersecting Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  mm->create_implicit_complement(); // create the implicit complement
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 6);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start = mm->bounding_box().center();
  start.x -= 10;
  Position end = mm->bounding_box().center();

  auto track_segments = xdg->segments(volume_id, start, end);

  double total_length {0.0};
  for (auto& segment : track_segments) {
    total_length += segment.second;
  }

  double exp_distance = mm->bounding_box().center().x - mm->bounding_box().min_x;
  REQUIRE(total_length == Catch::Approx(exp_distance).epsilon(0.00001));
}

TEST_CASE("Test Random Internal Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 4);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start {0.0, 0.0, 0.0};
  Position end {1.0, 1.0, 1.0};

  // create uniform distributions for each dimension of the bounding box
  auto bbox = mm->bounding_box();
  std::mt19937 gen(42); // Standard mersenne_twister_engine
  std::uniform_real_distribution<double> x_dist(bbox.min_x, bbox.max_x);
  std::uniform_real_distribution<double> y_dist(bbox.min_y, bbox.max_y);
  std::uniform_real_distribution<double> z_dist(bbox.min_z, bbox.max_z);

  int n_segments = 1000;
  for (int i = 0; i < n_segments; ++i) {
    start = {x_dist(gen), y_dist(gen), z_dist(gen)};
    end = {x_dist(gen), y_dist(gen), z_dist(gen)};

    std::cout << "Start: " << start << ", End: " << end << std::endl;

    // check that the segments are valid
    auto track_segments = xdg->segments(volume_id, start, end);
    // at least one segement should always be generated
    REQUIRE(track_segments.size() > 0);

    // because both points are in the mesh and the mesh is convex,
    // the length of the segments should be equal to the distance between the two points
    double total_length = std::accumulate(track_segments.begin(), track_segments.end(), 0.0,
      [](double sum, const std::pair<MeshID, double>& segment) {
        return sum + segment.second;
      });
    REQUIRE(total_length == Catch::Approx((start-end).length()).epsilon(0.00001));
  }
}