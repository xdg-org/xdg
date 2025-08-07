// stl includes
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/xdg.h"

#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Test Walk Elements") {
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 4);

  // lay a track onto the tet mesh
  Position r {-1.0, -2.0, -3.0};
  Direction u  = (mm->bounding_box().upper_right() - r).normalize();

  std::cout << "r: " << r << std::endl;
  std::cout << "u: " << u << std::endl;

  Position upper_right_corner = mm->bounding_box().upper_right();
  double distance = (upper_right_corner - r).length();

  std::cout << "Total distance: " << distance << std::endl;

  MeshID element = xdg->find_element(r);
  REQUIRE(element != ID_NONE);

  int max_iterations = 100;
  int iteration = 0;

  MeshID next_element = ID_NONE;
  double exit_distance = INFTY;
  while (element != ID_NONE && iteration < max_iterations) {

    std::cout << "Remaining distance: " << distance << std::endl;
    // if(distance <= TINY_BIT) break;

    std::cout << "---" << std::endl;
    std::cout << "Current element: " << element << std::endl;
    std::cout << r << std::endl;
    std::cout << u << std::endl;
    std::tie(next_element, exit_distance) = xdg->next_element(element, r, u);
    REQUIRE(exit_distance < distance);

    r += u * exit_distance;
    std::cout << "Next element: " << next_element << std::endl;
    std::cout << "Distance: " << exit_distance << std::endl;
    std::cout << "---" << std::endl;
    element = next_element;
    distance -= exit_distance;
  }

  REQUIRE(iteration < max_iterations);
  REQUIRE(next_element == ID_NONE);
  // if we've left the mesh, it should be at the upper right corner
  REQUIRE(r.x == Catch::Approx(upper_right_corner.x).epsilon(1e-04));
  REQUIRE(r.y == Catch::Approx(upper_right_corner.y).epsilon(1e-04));
  REQUIRE(r.z == Catch::Approx(upper_right_corner.z).epsilon(1e-04));
}
