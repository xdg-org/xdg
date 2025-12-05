// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

// xdg includes
#include "util.h"

using namespace xdg;
using namespace xdg::test;


// Expected values for the cube mesh without geometry
constexpr int32_t N_FACE_ELEMENTS = 1408;
constexpr int32_t N_VOLUME_ELEMENTS = 8814;

TEMPLATE_TEST_CASE("Test Mesh Without Geometry", "[mesh]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend); // skip if backend not enabled at configuration time
    auto mesh_manager = create_mesh_manager(mesh_backend);
    REQUIRE(mesh_manager);

    const std::string file = std::string("cube-mesh-no-geom.") +
      (mesh_backend == MeshLibrary::MOAB ? "h5m" : "exo");

    mesh_manager->load_file(file);
    mesh_manager->init();
    // there will be two volumes: one for the cube and one for the implicit complement
    REQUIRE(mesh_manager->num_volumes() == 2);
    // one surface (the boundary of the mesh) separates the two volumes
    REQUIRE(mesh_manager->num_surfaces() == 1);

    REQUIRE(mesh_manager->num_volume_elements(mesh_manager->implicit_complement()) == 0);
    REQUIRE(mesh_manager->num_volume_elements(1) == N_VOLUME_ELEMENTS);

    auto volume_surfaces = mesh_manager->get_volume_surfaces(1);
    REQUIRE(volume_surfaces.size() == 1);
    REQUIRE(mesh_manager->num_surface_faces(volume_surfaces[0]) == N_FACE_ELEMENTS);
  }
}
