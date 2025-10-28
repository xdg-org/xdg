#include <memory>
#include <iostream>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/geometry/closest.h"
#include "xdg/xdg.h"

// xdg test includes
#include "mesh_mock.h"
#include "util.h"

using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing, but its good practice to call it
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  xdg->prepare_raytracer();

  MeshID volume = mm->volumes()[0];

  Position origin {0.0, 0.0, 0.0};
  double nearest_distance {0.0};

  xdg->closest(volume, origin, nearest_distance);
  REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(2.0, 1e-6));

  // move the point closer to the positive x surface
  origin = {4.0, 0.0, 0.0};
  xdg->closest(volume, origin, nearest_distance);
  REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(1.0, 1e-6));

  // move the point outside of the volume, the same should apply
  origin = {10.0, 0.0, 0.0};
  xdg->closest(volume, origin, nearest_distance);
  REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(5.0, 1e-6));

  BoundingBox volume_box = mm->volume_bounding_box(volume);
  Position box_center = volume_box.center();

  int samples = 1000;
  Position p = box_center;
  for (int i = 0; i < samples; ++i) {
    p[0] = rand_double(-10.0, 10.0);
    xdg->closest(volume, p, nearest_distance);
    if (p[0] < box_center[0]) {
      REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(abs(p[0] - volume_box[0]), 1e-6));
    } else {
      REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(abs(p[0] - volume_box[0+3]), 1e-6));
    }
  }


}

TEST_CASE("Closest Point Unit Test")
{
  std::array<Position, 3> triangle {
    {Position {-1.0, -1.0, 0.0},
     Position {2.0, 1.0, 0.0},
     Position {-2.0, 3.0, 0.0}}
  };

  // ensure region classification is working correctly
  Position p = {0.0, 0.0, 0.0};
  Region region = determine_region(triangle, p);
  REQUIRE(region == Region::Zero);

  p = {3.0, 3.0, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::One);

  p = {-2.5, 3.5, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::Two);

  p = {-2.5, -1.5, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::Three);

  p = {-2.0, -2.0, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::Four);

  p = {1.0, -0.5, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::Five);

  p = {10.0, 0.0, 0.0};
  region = determine_region(triangle, p);
  REQUIRE(region == Region::Six);

  // ensure closest point is working correctly

  // start by testing the vertices, these should return exactly the same point
  p = triangle[0];
  Position closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest == p);

  p = triangle[1];
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest == p);

  p = triangle[2];
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest == p);

  // test midpoints of edges
  p = (triangle[0] + triangle[1]) / 2.0;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(p));

  p = (triangle[1] + triangle[2]) / 2.0;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(p));

  p = (triangle[2] + triangle[0]) / 2.0;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(p));

  // test a point on the triangle (region 0)
  p = {0.0, 0.0, 0.0};
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(p));

  // region testing
  p = {3.0, 1.0, 0.0}; // region 6
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(triangle[1]));

  p = {3.0, 1.0, 10.0}; // region 1/6
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(triangle[1]));

  p = {-1.0, -1.5, 0.0}; // region 4
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(triangle[0]));

  p = {-1.0, -1.5, 1.0}; // region 4
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(triangle[0]));

  // region 5
  double dist = 5.0;
  Position midpoint = (triangle[0] + triangle[1]) / 2.0;
  Direction perp_vec = triangle[1] - triangle[0];
  perp_vec[0] *= -1.0;
  std::swap(perp_vec[0], perp_vec[1]);
  perp_vec.normalize();
  p = midpoint + dist * perp_vec;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(midpoint));
  REQUIRE_THAT((closest - p).length(), Catch::Matchers::WithinAbs(dist, 1e-6));

  // region 1
  midpoint = (triangle[2] + triangle[1]) / 2.0;
  perp_vec = triangle[2] - triangle[1];
  perp_vec[0] *= -1.0;
  std::swap(perp_vec[0], perp_vec[1]);
  perp_vec.normalize();
  p = midpoint + dist * perp_vec;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(midpoint));
  REQUIRE_THAT((closest - p).length(), Catch::Matchers::WithinAbs(dist, 1e-6));

  // region 3
  midpoint = (triangle[0] + triangle[2]) / 2.0;
  perp_vec = triangle[0] - triangle[2];
  perp_vec[0] *= -1.0;
  std::swap(perp_vec[0], perp_vec[1]);
  perp_vec.normalize();
  p = midpoint + dist * perp_vec;
  closest = closest_location_on_triangle(triangle, p);
  REQUIRE(closest.approx_eq(midpoint));
  REQUIRE_THAT((closest - p).length(), Catch::Matchers::WithinAbs(dist, 1e-6));



}
