// for testing
#include <catch2/catch_test_macros.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/embree/ray_tracer.h"

#include "mesh_mocks.h"

using namespace xdg;

TEST_CASE("Test Mesh BVH")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MockedTriTetMesh>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  std::shared_ptr<RayTracer> rti = std::make_shared<EmbreeRayTracer>();

  std::unordered_map<MeshID, TreeID> volume_to_scene_map;
  for (auto volume: mm->volumes()) {
      auto [volume_tree, element_tree] = rti->register_volume(mm, volume);
    volume_to_scene_map[volume]= volume_tree;
  }

  REQUIRE(rti->num_registered_trees() == 2);
  REQUIRE(rti->num_registered_surface_trees() == 1);
  REQUIRE(rti->num_registered_element_trees() == 1);

  mm = std::make_shared<MockedTriTetMesh>();
  mm->init(); // this should do nothing

  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_faces(1) == 12);

  rti = std::make_shared<EmbreeRayTracer>();

  volume_to_scene_map.clear();
  for (auto volume: mm->volumes()) {
    auto [volume_tree, element_tree] = rti->register_volume(mm, volume);
    volume_to_scene_map[volume] = volume_tree;
  }
}