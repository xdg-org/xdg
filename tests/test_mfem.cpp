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

TEST_CASE("Test Ray Fire Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  int n_rays {1000};

  for (int i = 0; i < n_rays; i++) {
    direction = rand_dir();
    std::pair<double, MeshID> intersection;
    intersection = xdg->ray_fire(volume, origin, direction);
    if (intersection.second == ID_NONE)
      fatal_error("Ray did not intersect any geometry");
    if (intersection.first > 6.4) {
      fatal_error("Ray intersected geometry at distance greater than 6.4 cm");
    }
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(6.3849, 1e-1));
  }
}

TEST_CASE("Test Cylinder-Brick Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<MfemMeshManager>()};

  mesh_manager->load_file("cyl-brick.exo");

  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 3);

  REQUIRE(mesh_manager->num_surfaces() == 12);

  // get an element from each volume and check its volume ID
  auto vol1_elems = mesh_manager->get_volume_elements(1);
  REQUIRE(!vol1_elems.empty());

  auto vol2_elems = mesh_manager->get_volume_elements(2);
  REQUIRE(!vol2_elems.empty());

  // MFEM does not capture the right metadata for all this stuff!!!
  //
  //
  // mesh_manager->parse_metadata();

  // xdg::Property prop;

  // // check the model's metadata
  // prop = mesh_manager->get_volume_property(1, PropertyType::MATERIAL);
  // REQUIRE(prop.type == PropertyType::MATERIAL);
  // REQUIRE(prop.value == "steel");

  // prop = mesh_manager->get_volume_property(2, PropertyType::MATERIAL);
  // REQUIRE(prop.type == PropertyType::MATERIAL);
  // REQUIRE(prop.value == "iron");

  // for (auto s : mesh_manager->surfaces()) {
  //   prop = mesh_manager->get_surface_property(s, PropertyType::BOUNDARY_CONDITION);
  //   std::cout << s << ", " << prop.value << std::endl;
  //   REQUIRE(prop.type == PropertyType::BOUNDARY_CONDITION);
  //   if (s == 3) {
  //     REQUIRE(prop.value == "transmission");
  //   } else if (s == 4) {
  //     REQUIRE(prop.value == "reflective");
  //   } else {
  //     REQUIRE(prop.value == "vacuum");
  //   }
  // }
}
