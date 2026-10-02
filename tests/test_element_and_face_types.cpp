// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>


// xdg includes
#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "xdg/embree/ray_tracer.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test;

TEMPLATE_TEST_CASE("Test Element and Face Types Jezebel Tets", "[elements][faces][types][tets][tris]",
                   LibMesh_Interface, MOAB_Interface)
{
  constexpr auto mesh_backend = TestType::value;
  // skip if backend not enabled at configuration time
  check_mesh_library_supported(mesh_backend);
  std::string filename = mesh_backend == MeshLibrary::MOAB ? "jezebel.h5m" : "jezebel.exo";
  std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
  REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_backend);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file(filename);
  mesh_manager->init();

  for (const auto& volume : mesh_manager->volumes()) {
    auto elements = mesh_manager->get_volume_elements(volume);
    if (volume == mesh_manager->implicit_complement()) {
      continue;
    }
    REQUIRE(!elements.empty());
    for (const auto& element : elements) {
      REQUIRE(mesh_manager->element_type(element) == VolumeElementType::TET);
    }
  }

  for (const auto& surface : mesh_manager->surfaces()) {
    auto faces = mesh_manager->get_surface_faces(surface);
    REQUIRE(!faces.empty());
    for (const auto& face : faces) {
      REQUIRE(mesh_manager->face_type(face) == SurfaceFaceType::TRI);
    }
  }
}

TEMPLATE_TEST_CASE("Test Element and Face Types Jezebel Quads", "[elements][faces][types][quads][hexes]",
                   LibMesh_Interface, MOAB_Interface)
{
  constexpr auto mesh_backend = TestType::value;
  // skip if backend not enabled at configuration time
  check_mesh_library_supported(mesh_backend);
  std::string filename = mesh_backend == MeshLibrary::MOAB ? "jezebel-quads.h5m" : "jezebel-quads.exo";
  std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
  REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_backend);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file(filename);
  mesh_manager->init();

  for (const auto& volume : mesh_manager->volumes()) {
    auto elements = mesh_manager->get_volume_elements(volume);
    if (volume == mesh_manager->implicit_complement()) {
      continue;
    }
    REQUIRE(!elements.empty());
    for (const auto& element : elements) {
      REQUIRE(mesh_manager->element_type(element) == VolumeElementType::HEX);
    }
  }

  for (const auto& surface : mesh_manager->surfaces()) {
    auto faces = mesh_manager->get_surface_faces(surface);
    REQUIRE(!faces.empty());
    for (const auto& face : faces) {
      REQUIRE(mesh_manager->face_type(face) == SurfaceFaceType::QUAD);
    }
  }
}