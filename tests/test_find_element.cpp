// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"

#include "util.h"
#include "mesh_mock.h"

using namespace xdg;

TEST_CASE("Test Find Volumetric Element")
{
  // skip if Embree is not enabled
  check_ray_tracer_supported(RTLibrary::EMBREE);
  // create a mock mesh manager without volumetric elements
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  // Generate one test run per enabled backend
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);
  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);
    REQUIRE(rti);

    std::unordered_map<MeshID, TreeID> volume_to_scene_map;
    std::unordered_map<TreeID, TreeID> element_to_scene_map;
    for (auto volume: mm->volumes()) {
      auto [volume_tree, element_tree] = rti->register_volume(mm, volume);
      volume_to_scene_map[volume] = volume_tree;
      element_to_scene_map[volume_tree] = element_tree;
    }
    REQUIRE(rti->num_registered_trees() == 2);

    MeshID volume = 1;

    // test finding a volumetric element within the volume
    Position point_inside {0.0, 0.0, 0.0}; // point inside the volume
    MeshID element_id = rti->find_element(element_to_scene_map[volume], point_inside);
    REQUIRE(element_id != ID_NONE); // should find an element

    Position point_outside {10.0, 10.0, 10.0}; // point outside the volume
    element_id = rti->find_element(element_to_scene_map[volume], point_outside);
    REQUIRE(element_id == ID_NONE); // should not find an element since the point is outside the volume
  }
}


