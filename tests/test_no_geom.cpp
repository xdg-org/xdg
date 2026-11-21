// standard includes
#include <array>

// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "util.h"

using namespace xdg;
using namespace xdg::test;


// Expected values for the mesh file below
constexpr int32_t N_FACE_ELEMENTS = 1408;
constexpr int32_t N_VOLUME_ELEMENTS = 8814;
constexpr size_t CONNECTIVITY_CHECK_START = 1012;
constexpr std::array<std::array<MeshIndex, 4>, 10> SAMPLE_CONNECTIVITY = {{
  {{269, 125, 194, 271}},
  {{201, 269, 125, 194}},
  {{201, 125, 126, 127}},
  {{187, 125, 1079, 128}},
  {{187, 200, 189, 1079}},
  {{1080, 200, 953, 952}},
  {{1080, 200, 952, 970}},
  {{1080, 948, 952, 953}},
  {{951, 952, 938, 937}},
  {{951, 365, 952, 937}}
}};
struct ExpectedVertexCoordinate {
  MeshIndex index;
  std::array<double, 3> coords;
};

const std::array<ExpectedVertexCoordinate, 21> SAMPLE_COORDINATES = {{
  {125, {7.5, 5.435800689, 6.198253868}},
  {126, {6.198194777, 5.435745139, 7.5}},
  {127, {7.5, 4.5, 7.5}},
  {128, {7.5, 6.0, 7.5}},
  {187, {6.464635012, 6.362319508, 6.508808086}},
  {189, {6.0, 7.5, 7.5}},
  {194, {7.5, 3.847383127, 6.159340765}},
  {200, {6.198225749, 7.5, 5.435724846}},
  {201, {6.290991518, 4.687808951, 6.539017607}},
  {269, {6.227887345, 3.935549594, 5.261373429}},
  {271, {7.5, 4.855773298, 4.680674833}},
  {365, {4.999419144, 7.5, 3.077494412}},
  {937, {6.264800125, 7.5, 2.25}},
  {938, {7.5, 7.5, 3.0}},
  {948, {7.5, 6.253442111, 3.756374483}},
  {951, {6.137404653, 5.935482109, 2.849540536}},
  {952, {6.158642161, 7.5, 3.846974993}},
  {953, {7.5, 7.5, 4.5}},
  {970, {4.680834051, 7.5, 4.854218348}},
  {1079, {7.5, 7.5, 6.0}},
  {1080, {6.276147641, 6.66300095, 4.509159822}}
}};

TEMPLATE_LIST_TEST_CASE("Test Mesh Without Geometry", "[mesh]", MeshManagerBackends)
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

    // we should be able to successfully parse metadata even without geometry
    mesh_manager->parse_metadata();

    // there will be two volumes: one for the cube and one for the implicit complement
    REQUIRE(mesh_manager->num_volumes() == 2);
    // one surface (the boundary of the mesh) separates the two volumes
    REQUIRE(mesh_manager->num_surfaces() == 1);

    REQUIRE(mesh_manager->num_volume_elements(mesh_manager->implicit_complement()) == 0);
    REQUIRE(mesh_manager->num_volume_elements(1) == N_VOLUME_ELEMENTS);

    auto volume_surfaces = mesh_manager->get_volume_surfaces(1);
    REQUIRE(volume_surfaces.size() == 1);
    REQUIRE(mesh_manager->num_surface_faces(volume_surfaces[0]) == N_FACE_ELEMENTS);

    // spot check against known element connectivity and vertex coordinates
    for (size_t offset = 0; offset < SAMPLE_CONNECTIVITY.size(); ++offset) {
      MeshID element_id = mesh_manager->element_id(CONNECTIVITY_CHECK_START + offset);
      auto connectivity = mesh_manager->element_connectivity(element_id);
      REQUIRE(connectivity.size() == SAMPLE_CONNECTIVITY[offset].size());

      std::array<MeshIndex, 4> connectivity_indices {};
      for (size_t vertex_idx = 0; vertex_idx < connectivity.size(); ++vertex_idx) {
        connectivity_indices[vertex_idx] = mesh_manager->vertex_index(connectivity[vertex_idx]);
      }

      REQUIRE(connectivity_indices == SAMPLE_CONNECTIVITY[offset]);
    }

    constexpr double vertex_coord_tol = 1e-6;
    for (const auto& expected_vertex : SAMPLE_COORDINATES) {
      MeshID vertex_id = mesh_manager->vertex_id(expected_vertex.index);
      auto coords = mesh_manager->vertex_coordinates(vertex_id);

      REQUIRE_THAT(coords.x, Catch::Matchers::WithinAbs(expected_vertex.coords[0], vertex_coord_tol));
      REQUIRE_THAT(coords.y, Catch::Matchers::WithinAbs(expected_vertex.coords[1], vertex_coord_tol));
      REQUIRE_THAT(coords.z, Catch::Matchers::WithinAbs(expected_vertex.coords[2], vertex_coord_tol));
    }
  }
}
