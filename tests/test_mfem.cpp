// stl includes
#include <memory>


// testing includes
#include <catch2/catch_test_macros.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_managers.h"


using namespace xdg;

TEST_CASE("Test MFEM Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MfemMeshManager>();

  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volume_elements() == 10333);
}