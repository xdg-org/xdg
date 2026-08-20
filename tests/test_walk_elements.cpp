#include <memory>
#include <string>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

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
