#include <memory>
#include <string>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"

#include "util.h"
#include "../tools/walk_elements.h"

using namespace xdg;
using namespace xdg::test;

TEMPLATE_TEST_CASE("Test Hex Element Random Walk Jezebel Hexes",
                   "[walk_elements][hex][quads]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend);

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_backend);
    const auto& mesh_manager = xdg->mesh_manager();
    std::string file = "jezebel-quads";
    file += (mesh_backend == MeshLibrary::MOAB ? ".h5m" : ".exo");
    mesh_manager->load_file(file);
    mesh_manager->init();
    xdg->prepare_raytracer();

    WalkElementsContext context;
    context.xdg_ = xdg;
    context.n_threads_ = 1;
    context.n_particles_ = 9000;
    context.mean_free_path_ = 1.0;
    context.verbose_ = false;
    context.quiet_ = true;
    walk_elements(context);
  }
}


TEMPLATE_TEST_CASE("Test Hex Element Random Walk Jezebel Tets",
                   "[walk_elements][tet][tris]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend);

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_backend);
    const auto& mesh_manager = xdg->mesh_manager();
    std::string file = "jezebel";
    file += (mesh_backend == MeshLibrary::MOAB ? ".h5m" : ".exo");
    mesh_manager->load_file(file);
    mesh_manager->init();
    xdg->prepare_raytracer();

    WalkElementsContext context;
    context.xdg_ = xdg;
    context.n_threads_ = 1;
    context.n_particles_ = 9000;
    context.mean_free_path_ = 1.0;
    context.verbose_ = false;
    context.quiet_ = true;
    walk_elements(context);
  }
}

// Regression coverage for a LibMesh hex element whose quad exit face is split
// into two subtriangles. This ray intersects the outward-facing subtriangle;
// `next_element` used to miss that exit and return ID_NONE/INFTY when it chose
// the other subtriangle's orientation.
TEST_CASE("LibMesh next_element chooses exiting quad subtriangle",
          "[walk_elements][hex][quads][libmesh]")
{
  check_mesh_library_supported(MeshLibrary::LIBMESH);

  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel-quads.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  const MeshID element = 26960;
  const Position r {1.2606477549928472, -2.200610435773155, -0.9089446380562195};
  const Direction u {-0.9106430240324721, -0.13069809240463648, 0.391978687459897};

  REQUIRE(xdg->find_element(r) == element);

  const auto [next_element, exit_distance] = xdg->next_element(element, r, u);

  REQUIRE(next_element != ID_NONE);
  REQUIRE_THAT(exit_distance,
               Catch::Matchers::WithinAbs(0.10494826380348422, 1e-12));
}
