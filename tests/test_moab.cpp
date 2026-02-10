// stl includes
#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>

// testing includes
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/moab/mesh_manager.h"
#include "xdg/xdg.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test;

TEST_CASE("Test MOAB Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MOABMeshManager>();

  mesh_manager->load_file("pwr_pincell.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 4);
  REQUIRE(mesh_manager->num_surfaces() == 12);

  // create the implicit complement volume
  mesh_manager->create_implicit_complement();
  REQUIRE(mesh_manager->num_volumes() == 5);

  // parse metadata
  mesh_manager->parse_metadata();

  std::map<MeshID, std::string> material_exp_results =
    {
      {1, "UO2 (2.4%)"},
      {2, "Zircaloy"},
      {3, "Hot borated water"},
      {4, "void"},
      {5, "void"}
    };

  for (auto volume : mesh_manager->volumes()) {
    auto prop = mesh_manager->get_volume_property(volume, PropertyType::MATERIAL);
    REQUIRE(prop.type == PropertyType::MATERIAL);
    REQUIRE(material_exp_results[volume] == prop.value);
  }

  std::vector reflecting_surface_ids {2, 3, 14, 15, 17, 18};
  for (auto surface : reflecting_surface_ids) {
    auto prop = mesh_manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION);
    REQUIRE(prop.value == "reflecting");
  }

  // none of the volumes in this model should contain volumetric elements
  for (auto volume : mesh_manager->volumes()) {
    REQUIRE(mesh_manager->num_volume_elements(volume) == 0);
  }
}

TEMPLATE_TEST_CASE("Test BVH Build", "[moab][bvh]",
                   Embree_Raytracer,
                   GPRT_Raytracer)
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MOABMeshManager>();

  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);

  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto rti = create_raytracer(rt_backend);

    for (const auto& volume : mesh_manager->volumes()) {
      rti->register_volume(mesh_manager, volume);
    }
    REQUIRE(rti->num_registered_trees() == 2);
  }
}


TEMPLATE_TEST_CASE("Test Ray Fire MOAB (all built backends)", "[ray_tracer][moab]",
                   Embree_Raytracer,
                   GPRT_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    auto xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);

    const auto& mm = xdg->mesh_manager();
    mm->load_file("cube.h5m");
    mm->init();
    xdg->prepare_raytracer();

    MeshID volume = mm->volumes()[0];

    Position origin {0.0, 0.0, 0.0};
    Direction dir {1.0, 0.0, 0.0};

    auto hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

    origin = {3.0, 0.0, 0.0};
    hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(2.0, 1e-6));

    origin = {-10.0, 0.0, 0.0};
    hit = xdg->ray_fire(volume, origin, dir);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(15.0, 1e-6));

    origin = {0.0, 0.0, 0.0};
    REQUIRE(xdg->point_in_volume(volume, origin));
  }
}

TEST_CASE("MOAB Element Types")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cube.h5m");
  mesh_manager->init();

  for (const auto surface: mesh_manager->surfaces()) {
    auto element_type = mesh_manager->get_surface_element_type(surface);
    REQUIRE(element_type == SurfaceElementType::TRI); // Each surface should be made up of triangles
  }
}

TEST_CASE("MOAB Get Surface Connectivity", "[moab][surface]")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("small-tet-mesh.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 4);
  REQUIRE(mesh_manager->num_surface_faces(1) == 4);
  REQUIRE(mesh_manager->num_surface_faces(2) == 4);
  REQUIRE(mesh_manager->num_surface_faces(3) == 4);
  REQUIRE(mesh_manager->num_surface_faces(4) == 4);

  std::vector<std::vector<int>> expected_connectivity = {
    {3, 5, 0, 5, 4, 1, 4, 3, 2, 3, 4, 5}, /* Surface 0 (id 1) */
    {4, 3, 1, 3, 5, 0, 5, 4, 2, 4, 5, 3}, /* Surface 1 (id 2) */
    {4, 3, 0, 3, 5, 1, 5, 4, 2, 4, 5, 3}, /* Surface 2 (id 3) */
    {4, 3, 1, 3, 5, 0, 5, 4, 2, 4, 5, 3}  /* Surface 3 (id 4) */
  };

  size_t surface_index = 0;
  for (const auto surface : mesh_manager->surfaces()) {
    auto connectivity = mesh_manager->get_surface_connectivity(surface);
    REQUIRE(connectivity.size() == expected_connectivity[surface_index].size());
    for (size_t i = 0; i < connectivity.size(); ++i) {
      REQUIRE(connectivity[i] == expected_connectivity[surface_index][i]);
    }

    ++surface_index;
  }
}

TEST_CASE("MOAB Get Volume Connectivity", "[moab][volume]")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("small-tet-mesh.h5m");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 4);
  REQUIRE(mesh_manager->num_volume_elements(1) == 8);
  REQUIRE(mesh_manager->num_volume_elements(mesh_manager->implicit_complement()) == 0);

  std::vector<int> expected_connectivity = {
    0, 1, 2, 3,
    1, 4, 5, 6,
    2, 5, 7, 8,
    3, 6, 8, 9,
    8, 3, 2, 1,
    8, 2, 5, 1,
    8, 5, 6, 1,
    8, 6, 3, 1
  };

  REQUIRE(mesh_manager->volumes().size() == 2);
  auto connectivity = mesh_manager->get_volume_connectivity(mesh_manager->volumes()[0]);
  REQUIRE(connectivity.size() == expected_connectivity.size());
  for (size_t i = 0; i < connectivity.size(); ++i) {
    REQUIRE(connectivity[i] == expected_connectivity[i]);
  }
}

TEMPLATE_TEST_CASE("TEST MOAB Find Element Method", "[moab][elements]",
                   Embree_Raytracer)
{
  constexpr auto rt_backend = TestType::value;

  DYNAMIC_SECTION(fmt::format("Backend = {}", rt_backend)) {
    check_ray_tracer_supported(rt_backend); // skip if backend not enabled at configuration time
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB, rt_backend);
    REQUIRE(xdg->ray_tracing_interface()->library() == rt_backend);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel.h5m");
    mesh_manager->init();
    xdg->prepare_raytracer();

    MeshID volume = 1;
    REQUIRE(mesh_manager->num_volume_elements(1) == 10333);

    MeshID element = xdg->find_element(volume, {0.0, 0.0, 100.0});
    REQUIRE(element == ID_NONE); // should not find an element since the point is outside the volume

    element = xdg->find_element(volume, {0.0, 0.0, 0.0});
    REQUIRE(element != ID_NONE); // should find an element

    // test the next_element method
    auto next_element = xdg->mesh_manager()->next_element(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    REQUIRE(next_element.first != ID_NONE);
    REQUIRE(next_element.second != INFTY);

    // test the walk_elements method
    auto walk_elements = xdg->mesh_manager()->walk_elements(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 100.0);
    // get the sum of the distances
    double distance = std::accumulate(walk_elements.begin(), walk_elements.end(), 0.0,
                                      [](double total, const auto& segment) { return total + segment.second; });
    REQUIRE(distance > 0.0);
    REQUIRE(distance <= 100.0);
    for (const auto& segment : walk_elements) {
      REQUIRE(segment.first != ID_NONE);
      REQUIRE(segment.second >= 0.0);
    }
  }
}

TEST_CASE("MOAB Element ID and Index Mapping")
{
  // test mapping for contiguous MOAB IDs using jezebel model
  {
    std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
    REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MOAB);
    const auto& mesh_manager = xdg->mesh_manager();
    mesh_manager->load_file("jezebel.h5m");
    mesh_manager->init();

    size_t num_elements = mesh_manager->num_volume_elements();
    REQUIRE(num_elements == 10333);

    for (size_t idx = 0; idx < num_elements; ++idx) {
      MeshID element_id = mesh_manager->element_id(idx);
      // MOAB element IDs start at 1 and, for this model, are contiguous
      REQUIRE(element_id == idx + 1);
      int mapped_idx = mesh_manager->element_index(element_id);
      REQUIRE(mapped_idx == static_cast<int>(idx));
    }

    size_t num_vertices = mesh_manager->num_vertices();
    REQUIRE(num_vertices == 2067);
    for (size_t idx = 0; idx < num_vertices; ++idx) {
      MeshID vertex_id = mesh_manager->vertex_id(idx);
      // MOAB vertex IDs start at 1 and, for this model, are contiguous
      REQUIRE(vertex_id == idx + 1);
      int mapped_idx = mesh_manager->vertex_index(vertex_id);
      REQUIRE(mapped_idx == static_cast<int>(idx));
    }
  }

  // test mapping for non-contiguous MOAB IDs via modification
  {
    std::shared_ptr<MOABMeshManager> mesh_manager = std::make_shared<MOABMeshManager>();
    mesh_manager->load_file("jezebel.h5m");

    moab::Interface* mbi = mesh_manager->moab_interface();

    // create gaps in the intrinsic ID space by deleteing some elements
    moab::Range elem_range;
    mbi->get_entities_by_type(0, moab::MBTET, elem_range);
    int next_id = 0;
    std::vector<MeshID> modified_ids;
    for (const auto& elem : elem_range) {
      if (next_id % 100 == 0) {
        mbi->delete_entities(&elem, 1);
        next_id++;
        continue;
      }
      next_id ++;
      modified_ids.push_back(mbi->id_from_handle(elem));
    }

    mesh_manager->init();

    size_t num_elements = mesh_manager->num_volume_elements();
    REQUIRE(num_elements == modified_ids.size());
    for (size_t idx = 0; idx < num_elements; ++idx) {
      MeshID expected_id = modified_ids[idx];
      MeshID element_id = mesh_manager->element_id(idx);
      REQUIRE(element_id == expected_id);
      int mapped_idx = mesh_manager->element_index(element_id);
      REQUIRE(mapped_idx == static_cast<int>(idx));
    }

  }

  {
    std::shared_ptr<MOABMeshManager> mesh_manager = std::make_shared<MOABMeshManager>();
    mesh_manager->load_file("jezebel.h5m");

    moab::Interface* mbi = mesh_manager->moab_interface();

    moab::Range vertex_range;
    mbi->get_entities_by_type(0, moab::MBVERTEX, vertex_range);
    std::vector<MeshID> modified_vertex_ids;
    int next_id = 0;
    for (const auto& vertex : vertex_range) {
      if (next_id % 50 == 0) {
        // delete any elements adjacent to this vertex to avoid dangling references
        moab::Range adj_elems;
        mbi->get_adjacencies(&vertex, 1, 3, true, adj_elems);
        for (const auto& adj_elem : adj_elems) {
          mbi->delete_entities(&adj_elem, 1);
        }
        mbi->delete_entities(&vertex, 1);
        next_id++;
        continue;
      }
      modified_vertex_ids.push_back(mbi->id_from_handle(vertex));
      next_id++;
    }

    mesh_manager->init();

    size_t num_vertices = mesh_manager->num_vertices();
    REQUIRE(num_vertices == modified_vertex_ids.size());
    for (size_t idx = 0; idx < num_vertices; ++idx) {
      MeshID expected_id = modified_vertex_ids[idx];
      MeshID vertex_id = mesh_manager->vertex_id(idx);
      REQUIRE(vertex_id == expected_id);
      int mapped_idx = mesh_manager->vertex_index(vertex_id);
      REQUIRE(mapped_idx == static_cast<int>(idx));
    }
  }
}
