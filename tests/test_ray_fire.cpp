
// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/embree/ray_tracer.h"

#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Test Ray Fire Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>(false);
  mm->init(); // this should do nothing, just good practice to call it
  REQUIRE(mm->mesh_library() == MeshLibrary::INTERNAL);

  std::shared_ptr<RayTracer> rti = std::make_shared<EmbreeRayTracer>();
  auto [volume_tree, element_tree] = rti->register_volume(mm, mm->volumes()[0]);
  REQUIRE(volume_tree != ID_NONE);
  REQUIRE(element_tree == ID_NONE);

  Position origin {0.0, 0.0, 0.0};
  Direction direction {1.0, 0.0, 0.0};
  std::pair<double, MeshID> intersection;

  // fire from the origin toward each face, ensuring that the intersection distances are correct
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  direction *= -1;
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

  direction = {0.0, 1.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(6.0, 1e-6));

  direction *= -1;
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(3.0, 1e-6));

  direction = {0.0, 0.0, 1.0};
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(7.0, 1e-6));

  direction *= -1;
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(4.0, 1e-6));

  // fire from the outside of the cube toward each face, ensuring that the intersection distances are correct
  // rays should skip entering intersections and intersect with the far side of the cube
  origin = {-10.0, 0.0, 0.0};
  direction = {1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(15.0, 1e-6));

  origin = {10.0, 0.0, 0.0};
  direction = {-1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(12.0, 1e-6));

  // fire from the outside of the cube toward each face, ensuring that the intersection distances are correct
  // in this case rays are fired with a HitOrientation::ENTERING. Rays should hit the first surface intersected
  origin = {-10.0, 0.0, 0.0};
  direction = {1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::ENTERING);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(8.0, 1e-6));

  origin = {10.0, 0.0, 0.0};
  direction = {-1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::ENTERING);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // limit distance of the ray, shouldn't get a hit
  origin = {0.0, 0.0, 0.0};
  direction = {1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction, 4.5);
  REQUIRE(intersection.second == ID_NONE);

  // if the distance is just enough, we should still get a hit
  // limit distance of the ray, shouldn't get a hit
  origin = {0.0, 0.0, 0.0};
  direction = {1.0, 0.0, 0.0};
  intersection = rti->ray_fire(volume_tree, origin, direction, 5.1);
  REQUIRE(intersection.second != ID_NONE);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // Test excluding primitives, fire a ray from the origin and log the hit face
  // By providing the hit face as an excluded primitive in a subsequent ray fire,
  // there should be no intersection returned
  std::vector<MeshID> exclude_primitives;
  intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::EXITING, &exclude_primitives);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));
  REQUIRE(exclude_primitives.size() == 1);

  intersection = rti->ray_fire(volume_tree, origin, direction, INFTY, HitOrientation::EXITING, &exclude_primitives);
  REQUIRE(intersection.second == ID_NONE);
}
