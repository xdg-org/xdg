// stl includes
#include <array>
#include <memory>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/xdg.h"

#include "util.h"

using namespace xdg;
using namespace xdg::test;

namespace {

std::vector<XDGBackendFixture> make_rt_cases(
  const std::string& filename,
  MeshLibrary mesh_library = MeshLibrary::MOAB)
{
  std::vector<XDGBackendFixture> rt_cases;
  for (const auto& rt_entry : RT_LIB_TO_STR) {
    const auto rt_library = rt_entry.first;
    if (ray_tracer_available(rt_library)) {
      rt_cases.push_back(
        make_xdg_backend_fixture(mesh_library, rt_library, filename));
    }
  }

  return rt_cases;
}

} // namespace

TEST_CASE("Test Pincell RT libraries Cross-Check ray_fire queries", "[moab][rayfire][cross-check]")
{
  const auto rt_cases = make_rt_cases("pincell.h5m");
  if (rt_cases.size() < 2) {
    SKIP("Fewer than two ray tracing backends are available; skipping cross-check.");
  }

  srand48(12345); // set fixed seed for rng 
  std::vector<Direction> directions(1000);
  for (auto &dir : directions) {
    dir = rand_dir();
  }

  const Position origin {0.0, 0.0, 0.0};

  const auto& reference_case = rt_cases.front(); // This acts as our reference case

  for (const auto& volume : reference_case.xdg->mesh_manager()->volumes()) {
    for (const auto& direction : directions) {
      const auto reference_hit = reference_case.xdg->ray_fire(volume, origin, direction);

      for (size_t i = 1; i < rt_cases.size(); ++i) {
        const auto& candidate = rt_cases[i];
        const auto candidate_hit = candidate.xdg->ray_fire(volume, origin, direction);

        // Capture should print the backend configuration when the test fails
        CAPTURE(volume, direction, reference_case.label(), candidate.label(),
                reference_hit.first, reference_hit.second, candidate_hit.first, candidate_hit.second);
        REQUIRE(candidate_hit.second == reference_hit.second);
        if (reference_hit.second != ID_NONE) {
          REQUIRE_THAT(candidate_hit.first, Catch::Matchers::WithinAbs(reference_hit.first, 1e-6));
        }
      }
    }
  }
}

// TODO - Add all of the other queries
