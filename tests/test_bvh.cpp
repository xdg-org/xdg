// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Test Mesh BVH")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  // Generate one test run per enabled backend
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);

    std::unordered_map<MeshID, TreeID> volume_to_scene_map;
    for (auto volume: mm->volumes()) {
        auto [volume_tree, element_tree] = rti->register_volume(mm, volume);
      volume_to_scene_map[volume]= volume_tree;
    }

    REQUIRE(rti->num_registered_trees() == 2);
    REQUIRE(rti->num_registered_surface_trees() == 1);
    REQUIRE(rti->num_registered_element_trees() == 1);
  }
}