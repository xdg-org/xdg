#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"

#include "util.h"

using namespace xdg;
using namespace xdg::test;

struct ExpectedTrackSegment {
  MeshIndex element;
  double length;
};

struct SharedEdgeTrackCase {
  std::string name;
  MeshIndex start_element;
  Position start;
  Position end;
  std::vector<ExpectedTrackSegment> expected_segments;
};

constexpr double SIGNIFICANT_TRACK_LENGTH = 1.0e-8;

std::vector<std::pair<MeshID, double>>
significant_segments(const std::vector<std::pair<MeshID, double>> &segments) {
  std::vector<std::pair<MeshID, double>> result;
  std::copy_if(segments.begin(), segments.end(), std::back_inserter(result),
               [](const auto &segment) {
                 return std::abs(segment.second) > SIGNIFICANT_TRACK_LENGTH;
               });
  return result;
}

TEMPLATE_TEST_CASE("Test Regularized Tet Mesh Shared-Edge Tracks",
                   "[tracks][regression]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend);

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

    std::string filename = "regularized_tet_mesh";

    if (mesh_backend == MeshLibrary::MOAB) {
      filename += ".h5m";
    } else if (mesh_backend == MeshLibrary::LIBMESH) {
      filename += ".exo";
    } else {
      throw std::runtime_error(fmt::format(
          "Unsupported mesh library {} for regression test", mesh_backend));
    }

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend, RTLibrary::EMBREE);
    xdg->mesh_manager()->load_file(filename);
    xdg->mesh_manager()->init();

    for (const auto &test_case : cases) {
      DYNAMIC_SECTION(test_case.name) {
        MeshID start_element = xdg->mesh_manager()->element_id(test_case.start_element);
        const auto segments = significant_segments(xdg->mesh_manager()->walk_elements(
            start_element, test_case.start, test_case.end));

        REQUIRE(segments.size() == test_case.expected_segments.size());
        for (std::size_t i = 0; i < test_case.expected_segments.size(); ++i) {
          INFO(fmt::format("{} segment {}", test_case.name, i));
          // values in the hardcoded segment information are based on element indices in the mesh file, which may not match the element IDs used by the mesh manager. Use the mesh manager to get the correct element ID for comparison.
          MeshID exp_element_id = xdg->mesh_manager()->element_id(test_case.expected_segments[i].element);
          REQUIRE(exp_element_id == segments[i].first);
          REQUIRE(segments[i].second ==
                  Catch::Approx(test_case.expected_segments[i].length)
                      .margin(1.0e-12));
        }
      }
    }
  }
}

struct TrackCase {
  Position start;
  Position end;
};

double segment_sum(const std::vector<std::pair<MeshID, double>>& segments)
{
  return std::accumulate(segments.begin(), segments.end(), 0.0,
    [](double total, const auto& segment) { return total + segment.second; });
}


TEMPLATE_TEST_CASE("Segments account for simulated hex tracks starting on faces",
                   "[tracks][hex][regression]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend);

    std::string filename = "regularized_hex_mesh";
    if (mesh_backend == MeshLibrary::MOAB) {
      filename += ".h5m";
    } else if (mesh_backend == MeshLibrary::LIBMESH) {
      filename += ".exo";
    } else {
      throw std::runtime_error(fmt::format(
          "Unsupported mesh library {} for regression test", mesh_backend));
    }

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("regularized_hex_mesh.exo");
    mesh_manager->init();
    xdg->prepare_raytracer();

    const std::vector<TrackCase> cases {
      {{-6.0, 0.777417, -7.82735}, {-5.97095, 0.0210081, -7.17389}},
      {{6.0, 4.04678, 7.38517}, {5.1321, 4.14004, 6.89727}},
    };

    for (const auto& c : cases) {
      const Direction track = c.end - c.start;
      const double length = track.length();
      Direction u = track;
      u.normalize();

      DYNAMIC_SECTION(fmt::format("start = {}, end = {}", c.start, c.end))
      {
        CHECK(xdg->find_element(c.start + TINY_BIT * u) != ID_NONE);

        const auto bumped_segments = xdg->segments(c.start + TINY_BIT * u, c.end);
        CHECK_FALSE(bumped_segments.empty());
        CHECK_THAT(segment_sum(bumped_segments),
          Catch::Matchers::WithinAbs((c.end - (c.start + TINY_BIT * u)).length(),
            1.0e-12));

        const auto segments = xdg->segments(c.start, c.end);
        CHECK_FALSE(segments.empty());
        CHECK_THAT(segment_sum(segments),
          Catch::Matchers::WithinAbs(length, 1.0e-12));
      }
    }
  }
}


// Regression coverage for a hex element whose quad exit face is split
// into two subtriangles. This ray intersects the outward-facing subtriangle;
// `next_element` used to miss that exit and return ID_NONE/INFTY when it chose
// the other subtriangle's orientation.
TEMPLATE_TEST_CASE("Next_element chooses exiting quad subtriangle",
                   "[walk_elements][hex][quads]",
                   MOAB_Interface,
                   LibMesh_Interface)
{
  constexpr auto mesh_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", mesh_backend))
  {
    check_mesh_library_supported(mesh_backend);

    std::string filename = "jezebel-quads";
    if (mesh_backend == MeshLibrary::MOAB) {
      filename += ".h5m";
    } else if (mesh_backend == MeshLibrary::LIBMESH) {
      filename += ".exo";
    } else {
      throw std::runtime_error(fmt::format(
          "Unsupported mesh library {} for regression test", mesh_backend));
    }

    std::shared_ptr<XDG> xdg = XDG::create(mesh_backend);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(filename);
    mesh_manager->init();
    xdg->prepare_raytracer();

    const MeshIndex element_index = 26960;
    const Position r {1.2606477549928472, -2.200610435773155, -0.9089446380562195};
    const Direction u {-0.9106430240324721, -0.13069809240463648, 0.391978687459897};

    MeshID found_element = xdg->find_element(r);
    MeshIndex found_index = xdg->mesh_manager()->element_index(found_element);
    REQUIRE(found_index == element_index);

    const auto [next_element, exit_distance] = xdg->next_element(found_element, r, u);

    REQUIRE(next_element != ID_NONE);
    REQUIRE_THAT(exit_distance,
                Catch::Matchers::WithinAbs(0.10494826380348422, 1e-12));
  }
}
