#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"

#include "mesh_mock.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test;


TEMPLATE_TEST_CASE("Test Boundary Face Element Brick", "[boundary_faces]", MOAB_Interface, LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend)) {
    check_mesh_library_supported(mesh_backend);
    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_backend);
    const auto& mesh_manager = xdg->mesh_manager();

    const std::string filename = std::string("jezebel.") +
      (mesh_backend == MeshLibrary::MOAB ? "h5m" : "exo");
    mesh_manager->load_file(filename);
    mesh_manager->init();

    for (const auto surface : mesh_manager->surfaces()) {
      for (const auto face : mesh_manager->get_surface_faces(surface)) {
        const auto face_conn = mesh_manager->face_connectivity(face);
        const auto element = mesh_manager->get_boundary_face_element(face);

        REQUIRE(element != ID_NONE);

        const auto elem_conn = mesh_manager->element_connectivity(element);
        for (const auto vertex : face_conn) {
          REQUIRE(std::find(elem_conn.begin(), elem_conn.end(), vertex) != elem_conn.end());
        }
      }
    }
  }
}