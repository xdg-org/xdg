#include <algorithm>
#include <cmath>
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
#include "xdg/config.h"

#include "mesh_mocks.h"
#include "util.h"
#include "../tools/tally_segments.h"

using namespace xdg;
using namespace xdg::test;

namespace {

constexpr double SIGNIFICANT_TRACK_LENGTH = 1.0e-8;

struct ExpectedTrackSegment {
  MeshID element;
  double length;
};

struct SharedEdgeTrackCase {
  std::string name;
  MeshID start_element;
  Position start;
  Position end;
  std::vector<ExpectedTrackSegment> expected_segments;
};

std::vector<std::pair<MeshID, double>>
significant_segments(const std::vector<std::pair<MeshID, double>> &segments) {
  std::vector<std::pair<MeshID, double>> result;
  std::copy_if(segments.begin(), segments.end(), std::back_inserter(result),
               [](const auto &segment) {
                 return std::abs(segment.second) > SIGNIFICANT_TRACK_LENGTH;
               });
  return result;
}

} // namespace

TEST_CASE("Test Internal Tracks")
{
  std::shared_ptr<MockedTriTetMesh> mm = std::make_shared<MockedTriTetMesh>();
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
  std::shared_ptr<MockedTriTetMesh> mm = std::make_shared<MockedTriTetMesh>();
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
  std::shared_ptr<MockedTriTetMesh> mm = std::make_shared<MockedTriTetMesh>();
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

TEST_CASE("Test Random Jezebel Quad Tally Segments")
{
  check_mesh_library_supported(MeshLibrary::MOAB);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, RTLibrary::EMBREE);
  const auto& mm = xdg->mesh_manager();
  mm->load_file("jezebel-quads.h5m");
  mm->init();
  xdg->prepare_raytracer();

  TallyContext context;
  context.xdg_ = xdg;
  context.n_threads_ = 1;
  context.n_tracks_ = 5000;
  context.check_tracks_ = false;
  context.verbose_ = false;
  context.quiet_ = true;
  tally_segments(context);
}

TEST_CASE("Test Regularized Tet Mesh Shared-Edge Tracks", "[tracks]") {
  check_mesh_library_supported(MeshLibrary::LIBMESH);

  // These tracks were isolated from OpenMC unstructured mesh track output. Each
  // starts exactly on an internal tetrahedral edge shared by multiple tets. At
  // that point, several candidate exit faces can be hit at zero distance. The
  // walk must resolve that degenerate crossing so the first significant segment
  // lies in the element reached by moving forward along the ray, independent of
  // which incident tet is used as the starting element.
  const std::vector<SharedEdgeTrackCase> cases{
      {"track_1_1_106",
       6534,
       {0.0, 0.0, 0.85376076782158827},
       {3.5437529775821495, -1.720601311600237, 5.0},
       {{6548, 0.85249008092493106},
        {6549, 0.2649157923542812},
        {6551, 0.46369972968103246},
        {7741, 0.59171990796016161},
        {7740, 0.16712034187648464},
        {7746, 0.72715052471606312},
        {7747, 0.16070289924789175},
        {7757, 0.59960234790800471},
        {7763, 0.51247467567081428},
        {8953, 0.0057747215011482679},
        {8952, 0.67113585990588254},
        {8954, 0.29807051479411578},
        {8955, 0.40440425248856632}}},
      {"track_1_1_204",
       6534,
       {0.0, 0.0, 0.69534370559802172},
       {-2.9734003088237331, 5.0, -3.913390829505242},
       {{6655, 0.53707678810644066},
        {6654, 0.14355319119472715},
        {6648, 0.4391195403497975},
        {5458, 0.74186379941482283},
        {5459, 0.2202442264778236},
        {5457, 0.5564517046608578},
        {5456, 0.33036839242543642},
        {5570, 0.65796066035087675},
        {5568, 0.096588335150792903},
        {5569, 0.61723088717520735},
        {4379, 0.25553099580098859},
        {4373, 0.39606995453741162},
        {4363, 0.17936058477277245},
        {4362, 0.41342089677940774},
        {4365, 0.35251532806244718},
        {4479, 0.61631250675126337},
        {4478, 0.16253202584405196},
        {4476, 0.70549428871963782}}},
      {"track_1_1_320",
       8934,
       {0.0, 0.0, 4.3593067514614754},
       {-1.8266277097529049, -5.0, -4.8019522843691389},
       {{8934, 0.346476552969257},    {8928, 0.069082608619409014},
        {7738, 0.1034914838733231},   {7735, 0.39582754194372527},
        {7734, 1.3601819591640962},   {7728, 0.45361536976802003},
        {6538, 0.53298089563132567},  {6531, 0.94198732820120779},
        {6530, 0.034567881004307105}, {6416, 0.51985538905104145},
        {6409, 0.28372486086128257},  {5219, 0.96554942025127277},
        {5217, 0.12488604243264984},  {5216, 0.076125899143370826},
        {5212, 0.089054132545393169}, {5209, 0.38036510107412058},
        {5208, 0.67713575930235748},  {4018, 0.39597998090266584},
        {4011, 0.30992272063264537},  {4010, 0.41561231497787099},
        {3896, 0.77087556264604018},  {3889, 0.42072577558994134},
        {2699, 0.32136994038670175},  {2693, 0.60613453196465195}}},
      {"track_1_1_330",
       7734,
       {0.0, 0.0, 3.789238047880565},
       {-4.603666466975274, 1.9352074098218901, -4.8813964557793792},
       {{7851, 1.3486958215967388},   {7850, 0.33933558597643054},
        {7848, 0.37675338368475542},  {6658, 0.59324921375593997},
        {6651, 0.19822624960432911},  {6650, 0.20418476903684762},
        {6653, 0.51445818342263838},  {6652, 0.77203725007931645},
        {6642, 0.016884230482652484}, {6636, 0.0089646686941836605},
        {5446, 0.029260055470125987}, {5443, 1.0597255947782318},
        {5442, 0.4096141140789139},   {5436, 0.24950092664898027},
        {5437, 0.55990387409965592},  {4247, 0.66785288467696102},
        {4245, 0.030306747595857501}, {4244, 0.12045475198572025},
        {4240, 1.1944726086457138},   {4230, 0.19263707239002043},
        {4224, 0.10228049978163488},  {3034, 0.192536584479477},
        {3035, 0.054183534626086126}, {3033, 0.77041340268470349}}},
      {"track_1_1_650",
       8934,
       {0.0, 0.0, 4.4947561889510501},
       {1.3049550378936341, -5.0, -3.506971591620514},
       {{8944, 0.50637599158195812},  {8941, 0.082581902234627677},
        {7751, 0.11476626132841448},  {7745, 0.86626522598460431},
        {7744, 0.983358443813525},    {7741, 0.4164105801930052},
        {6551, 0.051756812169687014}, {6550, 0.27140989587526754},
        {6543, 0.2555301271353374},   {6542, 0.26164827796849682},
        {6428, 0.94805128689846563},  {6421, 0.59240411127229464},
        {5231, 0.69247151815595898},  {5230, 0.18035406257404638},
        {5223, 0.1698018283021652},   {5222, 1.2270207110819933},
        {5108, 0.06840715261814162},  {5101, 0.042745238587243391},
        {3911, 0.18514735400859961},  {3909, 1.1480388701336319},
        {3906, 0.17337175874181718},  {3900, 0.28734138505302786}}},
      {"track_1_1_678",
       6534,
       {0.0, 0.0, 0.75982698191287834},
       {2.988207546764408, -4.9911839451855986, -5.0},
       {{6544, 0.71104390421620278},
        {6541, 0.36889072663673012},
        {5351, 0.97194753549953894},
        {5350, 0.049595348586971399},
        {5343, 0.14278320376043144},
        {5342, 1.0360774296205704},
        {5228, 0.34404462750087939},
        {5221, 0.29813222282580815},
        {4031, 0.18124933405781105},
        {4030, 0.35046806648224149},
        {4027, 1.0248954205671967},
        {4037, 0.67651867930303433},
        {4034, 0.40502979758347341},
        {3920, 0.10951700094419738},
        {3913, 0.094902068856243971},
        {2723, 1.3273953324574725},
        {2721, 0.059064106804688918},
        {2720, 0.022665875869393445},
        {2716, 0.012164868765545251}}},
      {"track_1_1_830",
       8934,
       {0.0, 0.0, 4.2252633495664407},
       {-1.5941118114516291, 3.9918123153643208, -4.9999999999999991},
       {{9055, 0.17345855261867985},  {9054, 0.03844045084232374},
        {9048, 0.036615833223655213}, {7858, 0.051913585322124756},
        {7855, 1.4130813901934736},   {7854, 0.3797338828747393},
        {7848, 0.3617094029865861},   {6658, 0.51282820300886289},
        {6655, 0.28577977071138949},  {6654, 0.39041503022874308},
        {6657, 0.33061228467841153},  {6656, 0.35287903250411101},
        {6649, 0.33392394024540578},  {5459, 0.13222097192490578},
        {5457, 0.30556879682144111},  {5571, 0.75675195011812868},
        {5570, 0.47773051264200761},  {5568, 0.5341660298704406},
        {4378, 0.42012258320572743},  {4379, 0.44932556589581696},
        {4373, 0.13643708060959181},  {4372, 0.42877220867120436},
        {4369, 0.77178082299458273},  {3179, 0.33949822791579021},
        {3177, 0.20485635142282324},  {3176, 0.55886455134984947}}}};

  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::LIBMESH, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file("regularized_tet_mesh.exo");
  xdg->mesh_manager()->init();

  for (const auto &test_case : cases) {
    DYNAMIC_SECTION(test_case.name) {
      const auto segments = significant_segments(xdg->mesh_manager()->walk_elements(
          test_case.start_element, test_case.start, test_case.end));

      REQUIRE(segments.size() == test_case.expected_segments.size());
      for (std::size_t i = 0; i < test_case.expected_segments.size(); ++i) {
        INFO(fmt::format("{} segment {}", test_case.name, i));
        REQUIRE(segments[i].first == test_case.expected_segments[i].element);
        REQUIRE(segments[i].second ==
                Catch::Approx(test_case.expected_segments[i].length)
                    .margin(1.0e-12));
      }
    }
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
