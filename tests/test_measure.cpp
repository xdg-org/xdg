#include <memory>
#include <vector>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "xdg/xdg.h"

// xdg test includes
#include "mesh_mock.h"
#include "util.h"

using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init(); // this should do nothing, but it's good practice to call it

  // Generate one test run per enabled backend
  auto rt_backend = GENERATE(RTLibrary::EMBREE, RTLibrary::GPRT);

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time

    XDG xdg{mm, rt_backend};

    double volume = xdg.measure_volume(mm->volumes()[0]);
    REQUIRE_THAT(volume, Catch::Matchers::WithinAbs(693., 1e-6));

    double area = xdg.measure_volume_area(mm->volumes()[0]);
    REQUIRE_THAT(area, Catch::Matchers::WithinAbs(478., 1e-6));

    std::vector<double> surface_areas = {63., 63., 99., 99., 77., 77.};

    for (int i = 0; i < mm->surfaces().size(); ++i) {
      double area = xdg.measure_surface_area(mm->surfaces()[i]);
      REQUIRE_THAT(area, Catch::Matchers::WithinAbs(surface_areas[i], 1e-6));
    }
  }
}