#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "util.h"
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/omega_h/mesh_manager.h"
#include "xdg/xdg.h"

using namespace xdg;
using namespace xdg::test;

// These tests use "brick.exo".
// Its classification therefore describes one model volume bounded by six planar
// model surfaces. If the converted mesh in your test data differs, update the
// expected counts and ray-fire distances below to match.

TEST_CASE("Test Omega_h Initialization") {
  std::unique_ptr<MeshManager> mesh_manager =
      std::make_unique<OmegaHMeshManager>();

  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 5);
  REQUIRE(mesh_manager->num_surfaces() == 13);

  // init() already builds the implicit complement, so it should be counted
  REQUIRE(mesh_manager->implicit_complement() != ID_NONE);

  // num_ents_of_dimension should agree with the volume and surface counts
  REQUIRE(mesh_manager->num_ents_of_dimension(3) ==
          mesh_manager->num_volumes());
  REQUIRE(mesh_manager->num_ents_of_dimension(2) ==
          mesh_manager->num_surfaces());

  // every surface is bounded by the cube volume on its forward side
  MeshID volume = mesh_manager->volumes().front();
  for (auto surface : mesh_manager->get_volume_surfaces(volume)) {
    auto senses = mesh_manager->surface_senses(surface);
    REQUIRE((senses.first == volume || senses.second == volume));
  }
}

TEST_CASE("Omega_h Volume Elements") {
  std::unique_ptr<MeshManager> mesh_manager =
      std::make_unique<OmegaHMeshManager>();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  // unlike the MOAB surface meshes, an Omega_h mesh is volumetric: the cube
  // volume must contain tetrahedral elements
  MeshID volume = mesh_manager->volumes().front();
  auto elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(!elements.empty());
  REQUIRE(mesh_manager->num_volume_elements(volume) ==
          static_cast<int>(elements.size()));

  // the implicit complement holds no elements
  REQUIRE(mesh_manager->num_volume_elements(
              mesh_manager->implicit_complement()) == 0);

  // the global element count is the sum over all volumes and equals the number
  // of regions (tetrahedra) in the mesh
  int total = 0;
  for (auto v : mesh_manager->volumes()) {
    total += mesh_manager->num_volume_elements(v);
  }
  REQUIRE(mesh_manager->num_volume_elements() == total);
}

TEST_CASE("Omega_h Element Types") {
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::OMEGA_H);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::OMEGA_H);
  const auto &mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  // Omega_h simplex meshes use triangular surface elements throughout
  for (const auto surface : mesh_manager->surfaces()) {
    REQUIRE(mesh_manager->get_surface_element_type(surface) ==
            SurfaceElementType::TRI);
  }
}

TEST_CASE("Omega_h Connectivity") {
  std::unique_ptr<MeshManager> mesh_manager =
      std::make_unique<OmegaHMeshManager>();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  auto coords_match = [&](MeshID vertex, const Vertex &v) {
    auto expected = mesh_manager->vertex_coordinates(vertex);
    REQUIRE_THAT(v[0], Catch::Matchers::WithinAbs(expected[0], 1e-12));
    REQUIRE_THAT(v[1], Catch::Matchers::WithinAbs(expected[1], 1e-12));
    REQUIRE_THAT(v[2], Catch::Matchers::WithinAbs(expected[2], 1e-12));
  };

  // tetrahedra have four vertices; their coordinates must round-trip
  MeshID volume = mesh_manager->volumes().front();
  for (auto element : mesh_manager->get_volume_elements(volume)) {
    auto conn = mesh_manager->element_connectivity(element);
    REQUIRE(conn.size() == 4);

    auto verts = mesh_manager->element_vertices(element);
    REQUIRE(verts.size() == conn.size());
    for (std::size_t i = 0; i < conn.size(); ++i) {
      coords_match(conn[i], verts[i]);
    }

    // a non-degenerate tetrahedron has positive volume
    REQUIRE(std::abs(mesh_manager->element_volume(element)) > 0.0);
  }

  // triangular faces have three vertices; their coordinates must round-trip
  for (auto surface : mesh_manager->surfaces()) {
    for (auto face : mesh_manager->get_surface_faces(surface)) {
      auto conn = mesh_manager->face_connectivity(face);
      REQUIRE(conn.size() == 3);

      auto verts = mesh_manager->face_vertices(face);
      for (int i = 0; i < 3; ++i) {
        coords_match(conn[i], verts[i]);
      }

      // a boundary face is owned by exactly one element of the mesh
      REQUIRE(mesh_manager->get_boundary_face_element(face) != ID_NONE);
    }
  }
}

TEST_CASE("Omega_h Adjacency") {
  std::unique_ptr<MeshManager> mesh_manager =
      std::make_unique<OmegaHMeshManager>();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  constexpr unsigned int faces_per_tet = 4;
  MeshID volume = mesh_manager->volumes().front();

  for (auto element : mesh_manager->get_volume_elements(volume)) {
    for (int face = 0; face < faces_per_tet; ++face) {
      MeshID neighbor = mesh_manager->adjacent_element(element, face);
      // a neighbor is either another element or a boundary (ID_NONE), but never
      // the element itself
      REQUIRE(neighbor != element);
      if (neighbor != ID_NONE) {
        // adjacency is symmetric: the neighbor must list this element back
        bool reciprocal = false;
        for (int j = 0; j < faces_per_tet; ++j) {
          if (mesh_manager->adjacent_element(neighbor, j) == element) {
            reciprocal = true;
            break;
          }
        }
        REQUIRE(reciprocal);
      }
    }
  }
}

TEMPLATE_TEST_CASE("Test BVH Build Omega_h", "[omega_h][bvh]",
                   Embree_Raytracer) {
  constexpr auto rt_backend = TestType::value;

  std::shared_ptr<MeshManager> mesh_manager =
      std::make_shared<OmegaHMeshManager>();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 5);
  REQUIRE(mesh_manager->num_surfaces() == 13);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(
        rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);

    for (const auto &volume : mesh_manager->volumes()) {
      rti->register_volume(mesh_manager, volume);
    }
    REQUIRE(rti->num_registered_trees() == 2);
  }
}

TEMPLATE_TEST_CASE("Test Ray Fire Omega_h (all built backends)",
                   "[ray_tracer][omega_h]", Embree_Raytracer, GPRT_Raytracer) {
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(
        rt_backend); // skip if backend not enabled at configuration time
    auto xdg = XDG::create(MeshLibrary::OMEGA_H, rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::OMEGA_H);

    const auto &mm = xdg->mesh_manager();
    mm->load_file("pincell-implicit.exo");
    mm->init();

    xdg->prepare_raytracer();
    MeshID volume = mm->volumes()[0];

    Position origin{0.0, 0.0, 0.0};
    Direction dir{1.0, 0.0, 0.0};

    // the cube is 10 units on a side and centered at the origin
    auto hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE(hit.second != ID_NONE);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    origin = {3.0, 0.0, 0.0};
    hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

    origin = {0.0, 0.0, 0.0};
    REQUIRE(xdg->point_in_volume(volume, origin));
  }
}

TEMPLATE_TEST_CASE("Test Omega_h Find Element Method", "[omega_h][elements]",
                   Embree_Raytracer) {
  // Gold values for this test needs to be fixed. I will do that later.
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(
        rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::OMEGA_H, rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::OMEGA_H);
    const auto &mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("pincell-implicit.exo");
    mesh_manager->init();
    xdg->prepare_raytracer();

    MeshID volume = mesh_manager->volumes().front();

    MeshID element = xdg->find_element(
        volume, {0.0, 0.0, 100.0}); // I will need to fix this one as well
    REQUIRE(element == ID_NONE);    // point lies outside the cube

    element = xdg->find_element(volume, {0.0, 0.0, 0.0});
    REQUIRE(element != ID_NONE); // point lies inside the cube

    // test the next_element method
    auto next_element =
        mesh_manager->next_element(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    REQUIRE(next_element.first != ID_NONE);
    REQUIRE(next_element.second != INFTY);

    // test the walk_elements method across the full width of the cube
    auto walk_elements = mesh_manager->walk_elements(element, {0.0, 0.0, 0.0},
                                                     {0.0, 0.0, 1.0}, 100.0);
    double distance =
        std::accumulate(walk_elements.begin(), walk_elements.end(), 0.0,
                        [](double total, const auto &segment) {
                          return total + segment.second;
                        });
    REQUIRE(distance > 0.0);
    REQUIRE(distance <= 100.0);
    for (const auto &segment : walk_elements) {
      REQUIRE(segment.first != ID_NONE);
      REQUIRE(segment.second >= 0.0);
    }
  }
}

TEST_CASE("Omega_h Element ID and Index Mapping") {
  // Gold values for this test needs to be fixed. I will do that later.
  std::unique_ptr<MeshManager> mesh_manager =
      std::make_unique<OmegaHMeshManager>();
  REQUIRE(mesh_manager->mesh_library() == MeshLibrary::OMEGA_H);
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();

  // Omega_h stores entities in a contiguous, zero-based index space, so IDs
  and
      // indices are identical for both elements and vertices
      size_t num_elements = mesh_manager->num_volume_elements();
  REQUIRE(num_elements > 0);
  for (size_t idx = 0; idx < num_elements; ++idx) {
    MeshID element_id = mesh_manager->element_id(idx);
    REQUIRE(element_id == static_cast<MeshID>(idx));
    REQUIRE(mesh_manager->element_index(element_id) == static_cast<int>(idx));
  }

  size_t num_vertices = mesh_manager->num_vertices();
  REQUIRE(num_vertices > 0);
  for (size_t idx = 0; idx < num_vertices; ++idx) {
    MeshID vertex_id = mesh_manager->vertex_id(idx);
    REQUIRE(vertex_id == static_cast<MeshID>(idx));
    REQUIRE(mesh_manager->vertex_index(vertex_id) == static_cast<int>(idx));
  }
}

TEST_CASE("Test Track Exiting Mesh Omega_h") {
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::OMEGA_H);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::OMEGA_H);
  const auto &mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = mesh_manager->volumes().front();
  Position start{0.0, 0.0, -1000.0};
  Position end{0.0, 0.0, 1000.0};
  auto tracks = xdg->segments(volume, start, end);

  // the accumulated track length through the cube equals its 10 unit extent
  double length = std::accumulate(
      tracks.begin(), tracks.end(), 0.0,
      [](double sum, const auto &track) { return sum + track.second; });
  REQUIRE_THAT(length, Catch::Matchers::WithinAbs(10.0, 1e-6));
}