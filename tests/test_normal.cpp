// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/mesh_manager_interface.h"
#include "xdg/xdg.h"

#include "mesh_mock.h"
#include "util.h"

using namespace xdg;

TEST_CASE("Test Get Normal")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing, but its good practice to call it

  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  xdg->prepare_raytracer();

  MeshID volume = mm->volumes()[0];

  Position origin {0.0, 0.0, 0.0};
  double nearest_distance {0.0};

  // move the point closer to the positive x surface
  origin = {4.0, 0.0, 0.0};
  MeshID triangle;
  xdg->closest(volume, origin, nearest_distance, triangle);
  REQUIRE_THAT(nearest_distance, Catch::Matchers::WithinAbs(1.0, 1e-6));

  MeshID surface {mm->surfaces()[3]};

  // call for the normal w/o a triangle, it should be the same as the returned triangle from the closest call
  Direction normal = xdg->surface_normal(surface, origin);
  REQUIRE(normal == mm->face_normal(triangle));

  // move the origin, but pass the triangle
  // This should result in the same normal as well b/c the triangle is used intead of a call to 'closest'
  origin = {-2.0, 0.0, 0.0};
  std::vector<MeshID> exclude_primitives {triangle};
  normal = xdg->surface_normal(surface, origin, &exclude_primitives);
  REQUIRE(normal == mm->face_normal(triangle));
}