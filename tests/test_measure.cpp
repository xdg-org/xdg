#include <memory>
#include <vector>

// for testing
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "xdg/xdg.h"
#include "xdg/geometry/measure.h"

// xdg test includes
#include "mesh_mock.h"
#include "util.h"
#include "xdg/embree/ray_tracer.h"


using namespace xdg;

TEST_CASE("Test Mesh Mock")
{
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init();

  XDG xdg{mm, RTLibrary::EMBREE};

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

TEST_CASE("Test Mesh Mock Element Volume") {
  std::shared_ptr<MeshManager> mm = std::make_shared<MeshMock>();
  mm->init();

  // Test the element_volume method
  for (MeshID elem_id : mm->get_volume_elements(mm->volumes()[0])) {
    double vol = mm->element_volume(elem_id);
    REQUIRE_THAT(vol, Catch::Matchers::WithinAbs(57.75, 1e-6)); // Each tet has volume 57.75
  }
}

TEST_CASE("Test Area and Volume Surface Mesh")
{
  // skip if backend not enabled at configuration time
  check_mesh_library_supported(MeshLibrary::MOAB);
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube.h5m");

  mesh_manager->init();
  xdg->prepare_raytracer();

  auto bbox = mesh_manager->global_bounding_box();
  double model_length = bbox.width()[0];
  double model_width = bbox.width()[1];
  double model_height = bbox.width()[2];

  double exp_area = 2.0 * (model_length * model_width +
                           model_width * model_height +
                           model_length * model_height);

  for (const auto surface : mesh_manager->surfaces()) {
    double area = xdg->measure_surface_area(surface);
    std::cout << "Surface " << surface << " Area: " << area << ", Expected Area: " << exp_area / mesh_manager->num_surfaces() << std::endl;
    REQUIRE_THAT(area, Catch::Matchers::WithinAbs(exp_area / mesh_manager->num_surfaces(), 1e-6));
  }
}

TEST_CASE("Test Area and Volume Cube Mesh")
{
  auto test_pair = GENERATE(
    std::make_pair(MeshLibrary::MOAB, "cube-mesh-no-geom.h5m"),
    std::make_pair(MeshLibrary::LIBMESH, "cube-mesh-no-geom.exo")
  );

  DYNAMIC_SECTION(fmt::format("Backend = {}, File = {}", test_pair.first, test_pair.second)) {
    MeshLibrary mesh_library = test_pair.first;
    std::string file = test_pair.second;
    // skip if backend not enabled at configuration time
    check_mesh_library_supported(mesh_library);
    std::shared_ptr<XDG> xdg = XDG::create(mesh_library);
    REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_library);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(file);
    mesh_manager->init();
    xdg->prepare_raytracer();

    // Determine model dimensions based on the width of the bounding box
    auto bbox = mesh_manager->global_bounding_box();
    double model_length = bbox.width()[0];
    double model_width = bbox.width()[1];
    double model_height = bbox.width()[2];

    double exp_area = 2.0 * (model_length * model_width +
                             model_width * model_height +
                             model_length * model_height);
    double exp_volume = model_length * model_width * model_height;

    // Measure the surface area of the cube
    double total_surface_area = 0.0;
    for (const auto surface : mesh_manager->surfaces()) {
      double area = xdg->measure_surface_area(surface);
      total_surface_area += area;
    }
    std::cout << "Total Surface Area: " << total_surface_area << ", Expected Surface Area: " << exp_area << std::endl;
    REQUIRE_THAT(total_surface_area, Catch::Matchers::WithinAbs(exp_area, 1e-6));

    // Measure the volume of the cube
    MeshID volume = 1;
    double total_volume = xdg->measure_volume(volume);
    std::cout << "Total Volume: " << total_volume << ", Expected Volume: " << exp_volume << std::endl;
    REQUIRE_THAT(total_volume, Catch::Matchers::WithinAbs(exp_volume, 1e-6));

    // Measure the volume of the cube by summing element volumes
    double sum_element_volumes = 0.0;
    for (const auto element : mesh_manager->get_volume_elements(volume)) {
      double elem_volume = mesh_manager->element_volume(element);
      sum_element_volumes += elem_volume;
    }

    std::cout << "Sum of Element Volumes: " << sum_element_volumes << ", Expected Volume: " << exp_volume << std::endl;
    REQUIRE_THAT(sum_element_volumes, Catch::Matchers::WithinAbs(exp_volume, 1e-6));
  }
}

TEST_CASE("Test Area and Volume Spherical Mesh") {

  auto test_pair = GENERATE(
    std::make_pair(MeshLibrary::MOAB, "jezebel.h5m"),
    std::make_pair(MeshLibrary::LIBMESH, "jezebel.exo")
  );

  DYNAMIC_SECTION(fmt::format("Backend = {}, File = {}", test_pair.first, test_pair.second)) {
    MeshLibrary mesh_library = test_pair.first;
    std::string file = test_pair.second;
    // skip if backend not enabled at configuration time
    check_mesh_library_supported(mesh_library);
    std::shared_ptr<XDG> xdg = XDG::create(mesh_library);
    REQUIRE(xdg->mesh_manager()->mesh_library() == mesh_library);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file(file);
    mesh_manager->init();
    xdg->prepare_raytracer();

    // Determine model radius based on the width of the bounding box
    auto bbox = mesh_manager->global_bounding_box();
    double model_radius = 0.5 * bbox.width()[0];
    double exp_area = 4.0 * M_PI * std::pow(model_radius, 2);
    double exp_volume = (4.0 / 3.0) * M_PI * std::pow(model_radius, 3);

    // Measure the surface area of the sphere
    double total_surface_area = 0.0;
    for (const auto surface : mesh_manager->surfaces()) {
      double area = xdg->measure_surface_area(surface);
      total_surface_area += area;
    }
    std::cout << "Total Surface Area: " << total_surface_area << ", Expected: " << exp_area << std::endl;
    // high tolerance due to coarse mesh
    REQUIRE_THAT(total_surface_area, Catch::Matchers::WithinAbs(exp_area, 1.0));

    // Measure the volume of the sphere
    MeshID volume = 1;
    double total_volume = xdg->measure_volume(volume);
    std::cout << "Total Volume: " << total_volume << ", Expected: " << exp_volume << std::endl;
    // high tolerance due to coarse mesh
    REQUIRE_THAT(total_volume, Catch::Matchers::WithinAbs(exp_volume, 10.0));

    // Check that the sum of element volumes matches the total volume
    double sum_element_volumes = 0.0;
    for (const auto element : mesh_manager->get_volume_elements(volume)) {
      double elem_volume = mesh_manager->element_volume(element);
      sum_element_volumes += elem_volume;
    }
    std::cout << "Sum of Element Volumes: " << sum_element_volumes << ", Total Volume: " << total_volume << std::endl;
    // these should match closely since we're summing element volumes that fill the surface mesh volume
    REQUIRE_THAT(sum_element_volumes, Catch::Matchers::WithinAbs(total_volume, 1e-6));
  }
}

