// stl includes
#include <memory>


// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "util.h"


using namespace xdg;

TEST_CASE("Test MFEM Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MfemMeshManager>();

  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volume_elements() == 16624);

  // property type
}

// Read in the brick, read the element type and check it matches what
// we are expecting.
// The brick is meshed with tets
TEST_CASE("MFEM element types")
{
  std::unique_ptr<MfemMeshManager> mesh_manager = std::make_unique<MfemMeshManager>();
  mesh_manager->load_file("brick.exo");

  mesh_manager->init();
  REQUIRE(mesh_manager->num_volume_elements() == 8790);

  // At time of writing, brick.exo does not have sidesets labelled, so we just check
  // each of the elements
  for (int i=0; i<mesh_manager->num_boundary_elements(); i++) 
    REQUIRE( mesh_manager->get_surface_element_type(i) == SurfaceElementType::TRI );

}

// next, emulate the Find Element Method
TEST_CASE("TEST MFEM Find Element Method")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);

  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();

  size_t num_elements = mesh_manager->num_volume_elements();
  REQUIRE(num_elements == 10333);

  xdg->prepare_raytracer();

  MeshID volume = 1;
  MeshID element = xdg->find_element(volume, {0.0, 0.0, 100.0});
  REQUIRE(element == ID_NONE); // should not find an element since the point is outside the volume

  element = xdg->find_element(volume, {0.0, 0.0, 0.0});
  REQUIRE(element != ID_NONE); // should find an element

  auto next_element = xdg->mesh_manager()->next_element(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
  REQUIRE(next_element.first != ID_NONE);
  REQUIRE(next_element.second != INFTY);
}

TEST_CASE("TEST Ray Fire Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);

  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};
  std::pair<double, MeshID> intersection;

  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));
}
