#include <memory>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mesh_mock.h"

#include "xdg/ray_tracing_interface.h"

using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  // get a random element from volume 1 (we know there are 12 elements)
  MeshID random_element = 5; // arbitrarily choosing element 5
  REQUIRE(mm->element_volume_id(random_element) == 1);
}

