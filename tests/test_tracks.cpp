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


TEST_CASE("Test Internal Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 4);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start {0.0, 0.0, 0.0};
  Position end {1.0, 1.0, 1.0};

  auto track_segments = xdg->segments(volume_id, start, end);

  double total_length {0.0};
  for (auto& segment : track_segments) {
    total_length += segment.second;
  }
  REQUIRE(total_length == Catch::Approx((start-end).length()).epsilon(0.00001));
}

TEST_CASE("Test Intersecting Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  mm->create_implicit_complement(); // create the implicit complement
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 6);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start = mm->bounding_box().center();
  start.x -= 10;
  Position end = mm->bounding_box().center();

  auto track_segments = xdg->segments(volume_id, start, end);

  double total_length {0.0};
  for (auto& segment : track_segments) {
    total_length += segment.second;
  }

  double exp_distance = mm->bounding_box().center().x - mm->bounding_box().min_x;
  REQUIRE(total_length == Catch::Approx(exp_distance).epsilon(0.00001));
}

TEST_CASE("Test Random Internal Tracks")
{
  std::shared_ptr<MeshMock> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing
  std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mm);
  REQUIRE(mm->num_volumes() == 1);
  REQUIRE(mm->num_surfaces() == 6);
  REQUIRE(mm->num_volume_elements(1) == 12); // should return 12 volumetric elements

  xdg->prepare_raytracer();
  REQUIRE(xdg->ray_tracing_interface()->num_registered_trees() == 4);

  // lay a track onto the tet mesh
  MeshID volume_id = 0;
  Position start {0.0, 0.0, 0.0};
  Position end {1.0, 1.0, 1.0};

  // create uniform distributions for each dimension of the bounding box
  auto bbox = mm->bounding_box();
  std::mt19937 gen(42); // Standard mersenne_twister_engine
  std::uniform_real_distribution<double> x_dist(bbox.min_x, bbox.max_x);
  std::uniform_real_distribution<double> y_dist(bbox.min_y, bbox.max_y);
  std::uniform_real_distribution<double> z_dist(bbox.min_z, bbox.max_z);

  int n_segments = 1000;
  for (int i = 0; i < n_segments; ++i) {
    start = {x_dist(gen), y_dist(gen), z_dist(gen)};
    end = {x_dist(gen), y_dist(gen), z_dist(gen)};

    // check that the segments are valid
    auto track_segments = xdg->segments(volume_id, start, end);
    // at least one segement should always be generated
    REQUIRE(track_segments.size() > 0);

    // because both points are in the mesh and the mesh is convex,
    // the length of the segments should be equal to the distance between the two points
    double total_length = std::accumulate(track_segments.begin(), track_segments.end(), 0.0,
      [](double sum, const std::pair<MeshID, double>& segment) {
        return sum + segment.second;
      });
    REQUIRE(total_length == Catch::Approx((start-end).length()).epsilon(0.00001));
  }
}

TEMPLATE_TEST_CASE("Test Single-Tet Glancing Vertex Intersection Tracks", "[tracks]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    // Run this geometry case for each mesh backend that is available in the build.
    check_mesh_library_supported(mesh_backend);
    check_ray_tracer_supported(RTLibrary::EMBREE);

    // Load the same single-tetrahedron mesh through the backend's native file format.
    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend, RTLibrary::EMBREE);
    const auto& mm = xdg->mesh_manager();
    const std::string file = std::string("single-tet.") +
      (mesh_backend == MeshLibrary::MOAB ? "h5m" : "exo");
    mm->load_file(file);
    mm->init();
    mm->parse_metadata();

    MeshID volume = 1;
    // Ensure that the volume is the intended volume containing a single tet
    REQUIRE(mm->num_volume_elements(volume) == 1);
    const auto volume_elements = mm->get_volume_elements(volume);
    const auto tet_vertices = mm->element_vertices(volume_elements.front());

    xdg->prepare_raytracer();

    // Make the track long enough to cross the whole model to ensure intersection
    const auto bbox = mm->global_bounding_box();
    const double track_length = bbox.max_chord_length() * 2.0;
    const double half_length = 0.5 * track_length;
    size_t n_vertex_tangent_cases = 0;

    // Build tracks through face vertices along each face normal. These lines touch
    // the tetrahedron at a vertex only, so they should tally no element length.
    for (const auto surface : mm->get_volume_surfaces(volume)) {
      for (const auto face : mm->get_surface_faces(surface)) {
        const Direction direction = mm->face_normal(face);
        const auto vertices = mm->face_vertices(face);

        for (const auto& vertex : vertices) {
          // Exclude tracks that also run along a tet edge; those are edge-overlap
          // cases and can have nonzero edge length even though they pass a vertex.
          const auto line_contains_tet_edge = std::any_of(tet_vertices.begin(), tet_vertices.end(),
              [&](const Vertex& tet_vertex) {
                if (tet_vertex == vertex) return false;
                return ((tet_vertex - vertex).cross(direction)).length() < 1e-12;
              });
          if (line_contains_tet_edge) continue;

          const Position start = vertex - direction * half_length;
          const Position end = vertex + direction * half_length;

          std::vector<std::pair<MeshID, double>> track_segments;
          REQUIRE_NOTHROW([&]() { track_segments = xdg->segments(start, end); }());

          const double total_length = std::accumulate(
              track_segments.begin(), track_segments.end(), 0.0,
              [](double sum, const std::pair<MeshID, double>& segment) {
                return sum + segment.second;
              });

          INFO(fmt::format("Backend = {}, Face = {}, Vertex = {}", mesh_backend, face, vertex));
          ++n_vertex_tangent_cases;
          // A vertex-only tangent contact should effectivly contribute zero distance to the tet.
          REQUIRE(total_length == Catch::Approx(0.0).margin(1e-12));

        }
      }
    }
    // Guard against accidentally filtering out the entire test fixture.
    REQUIRE(n_vertex_tangent_cases > 0);
  }
}

TEMPLATE_TEST_CASE("Test Single-Tet Vertex Intersection Tracks", "[tracks]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    // Run this geometry case for each mesh backend that is available in the build.
    check_mesh_library_supported(mesh_backend);
    check_ray_tracer_supported(RTLibrary::EMBREE);

    // Load the same single-tetrahedron mesh through the backend's native file format.
    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend, RTLibrary::EMBREE);
    const auto& mm = xdg->mesh_manager();
    const std::string file = std::string("single-tet.") +
      (mesh_backend == MeshLibrary::MOAB ? "h5m" : "exo");
    mm->load_file(file);
    mm->init();
    mm->parse_metadata();

    MeshID volume = 1;
    // Ensure that the volume is the intended volume containing a single tet
    REQUIRE(mm->num_volume_elements(volume) == 1);
    const auto volume_elements = mm->get_volume_elements(volume);
    const auto tet_vertices = mm->element_vertices(volume_elements.front());

    xdg->prepare_raytracer();

    // The centroid is strictly inside the tet, so a track from the centroid
    // through any vertex should tally the centroid-to-vertex distance.
    Position centroid {0.0, 0.0, 0.0};
    for (const auto& vertex : tet_vertices) {
      centroid += vertex;
    }
    centroid /= static_cast<double>(tet_vertices.size());

    const auto bbox = mm->global_bounding_box();
    const double outside_extension = bbox.max_chord_length();

    for (const auto& vertex : tet_vertices) {
      const Direction direction = (vertex - centroid).normalize();
      const Position outside = vertex + direction * outside_extension;
      const double expected_length = (vertex - centroid).length();

      // This is the directed vertex-crossing test: first tally from the
      // interior centroid out through the vertex, then supply the same segment
      // in reverse and require an identical tallied distance.
      std::vector<std::pair<MeshID, double>> forward_segments;
      std::vector<std::pair<MeshID, double>> reverse_segments;
      REQUIRE_NOTHROW([&]() { forward_segments = xdg->segments(centroid, outside); }());
      REQUIRE_NOTHROW([&]() { reverse_segments = xdg->segments(outside, centroid); }());

      const double forward_length = std::accumulate(
          forward_segments.begin(), forward_segments.end(), 0.0,
          [](double sum, const std::pair<MeshID, double>& segment) {
            return sum + segment.second;
          });
      const double reverse_length = std::accumulate(
          reverse_segments.begin(), reverse_segments.end(), 0.0,
          [](double sum, const std::pair<MeshID, double>& segment) {
            return sum + segment.second;
          });

      INFO(fmt::format("Backend = {}, Vertex = {}", mesh_backend, vertex));
      REQUIRE(forward_length == Catch::Approx(expected_length).epsilon(0.00001));
      REQUIRE(reverse_length == Catch::Approx(expected_length).epsilon(0.00001));
      REQUIRE(forward_length == Catch::Approx(reverse_length).epsilon(0.00001));
    }
  }
}
