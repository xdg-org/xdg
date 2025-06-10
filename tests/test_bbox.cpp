#include <sstream>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/bbox.h"
#include "xdg/vec3da.h"

using namespace xdg;


TEST_CASE("Test BoundingBox")
{

  // set the precision for floating point number output to 15 decimal places
  Catch::StringMaker<double>::precision = 15;

  BoundingBox a {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};

  BoundingBox b {-1.0, -1.0, -1.0, 0.0, 0.0, 0.0};

  BoundingBox expanded_box {-1.0, -1.0, -1.0, 1.0, 1.0, 1.0};

  BoundingBox c = a;
  REQUIRE(c == a);
  c.update(b);
  REQUIRE(c == expanded_box);
  c = b;
  REQUIRE(c == b);
  c.update(a);
  REQUIRE(c == expanded_box);

  Position p1 {-2.0, -3.0, -4.0};
  c.update(p1);
  REQUIRE(c.min_x == -2.0);
  REQUIRE(c.min_y == -3.0);
  REQUIRE(c.min_z == -4.0);

  Position p2 {5.0, 6.0, 7.0};
  c.update(p2);
  REQUIRE(c.max_x == 5.0);
  REQUIRE(c.max_y == 6.0);
  REQUIRE(c.max_z == 7.0);

  // Test center() method
  Position center = c.center();
  REQUIRE(center.x == 1.5);
  REQUIRE(center.y == 1.5);
  REQUIRE(center.z == 1.5);

  // Test width() method
  Vec3da width = c.width();
  REQUIRE(width.x == 7.0);
  REQUIRE(width.y == 9.0);
  REQUIRE(width.z == 11.0);

  // Test lower_left() method
  Position lower = c.lower_left();
  REQUIRE(lower.x == -2.0);
  REQUIRE(lower.y == -3.0);
  REQUIRE(lower.z == -4.0);

  // Test upper_right() method
  Position upper = c.upper_right();
  REQUIRE(upper.x == 5.0);
  REQUIRE(upper.y == 6.0);
  REQUIRE(upper.z == 7.0);

  // Test contains() method
  Position inside {0.0, 0.0, 0.0};
  Position outside {10.0, 10.0, 10.0};
  REQUIRE(c.contains(inside));
  REQUIRE_FALSE(c.contains(outside));

  // Test max_chord_length() method
  REQUIRE(a.max_chord_length() == std::sqrt(3.0));
  REQUIRE(b.max_chord_length() == std::sqrt(3.0));
  // box c at this point is 7x9x11
  REQUIRE(c.max_chord_length() == std::sqrt(49.0 + 81.0 + 121.0));

  // Test dilation() method
  REQUIRE(a.dilation() == std::sqrt(3.0) * std::pow(10, -std::numeric_limits<float>::digits10));
  REQUIRE(b.dilation() == std::sqrt(3.0) * std::pow(10, -std::numeric_limits<float>::digits10));
  REQUIRE(c.dilation() == std::sqrt(49.0 + 81.0 + 121.0) * std::pow(10, -std::numeric_limits<float>::digits10));

  // Test from_points() static method
  std::vector<Position> points = {
    Position{1.0, 2.0, 3.0},
    Position{4.0, 5.0, 6.0},
    Position{-1.0, -2.0, -3.0}
  };
  BoundingBox from_points_box = BoundingBox::from_points(points);
  REQUIRE(from_points_box.min_x == -1.0);
  REQUIRE(from_points_box.min_y == -2.0);
  REQUIRE(from_points_box.min_z == -3.0);
  REQUIRE(from_points_box.max_x == 4.0);
  REQUIRE(from_points_box.max_y == 5.0);
  REQUIRE(from_points_box.max_z == 6.0);

  // Test operator<<
  std::stringstream ss;
  ss << c;
  CHECK_THAT(ss.str(), Catch::Matchers::Equals("Lower left: -2, -3, -4, Upper right: 5, 6, 7"));

  // Stress test with large numbers
  BoundingBox large_box{1e10, 1e10, 1e10, 2e10, 2e10, 2e10};
  REQUIRE(large_box.width().x == 1e10);
  REQUIRE(large_box.center().x == 1.5e10);

  // Stress test with very small numbers
  BoundingBox small_box{1e-10, 1e-10, 1e-10, 2e-10, 2e-10, 2e-10};
  REQUIRE(small_box.width().x == 1e-10);
  REQUIRE(small_box.center().x == 1.5e-10);

  // Stress test with mixed positive/negative numbers
  BoundingBox mixed_box{-1e10, -1e10, -1e10, 1e10, 1e10, 1e10};
  REQUIRE(mixed_box.width().x == 2e10);
  REQUIRE(mixed_box.center().x == 0.0);
  REQUIRE(mixed_box.min_x == -1e10);
  REQUIRE(mixed_box.min_y == -1e10);
  REQUIRE(mixed_box.min_z == -1e10);
  REQUIRE(mixed_box.max_x == 1e10);
  REQUIRE(mixed_box.max_y == 1e10);
  REQUIRE(mixed_box.max_z == 1e10);

  // Stress test update() with extreme values
  BoundingBox stress_box;
  stress_box.update(Position{1e20, 1e20, 1e20});
  stress_box.update(Position{-1e20, -1e20, -1e20});
  REQUIRE(stress_box.width().x == 2e20);
  REQUIRE(stress_box.center().x == 0.0);

  // Stress test from_points() with many points
  std::vector<Position> many_points;
  for(int i = 0; i < 1000; i++) {
    many_points.push_back(Position{i * 1e5, i * 1e5, i * 1e5});
  }
  BoundingBox many_points_box = BoundingBox::from_points(many_points);
  REQUIRE(many_points_box.min_x == 0.0);
  REQUIRE(many_points_box.max_x == 999e5);
}