// stl includes
#include <algorithm>
#include <map>
#include <memory>
#include <set>


// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/error.h"
#include "xdg/geometry/face_common.h"
#include "xdg/mesh_managers.h"
#include "xdg/xdg.h"
#include "util.h"


using namespace xdg;

TEST_CASE("Test MFEM Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager = std::make_unique<MfemMeshManager>();

  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volume_elements() == 16624);

  // property type
}

// Read in the brick, read the element type and check it matches what
// we are expecting.
// The brick is meshed with tets
TEST_CASE("MFEM element types")
{
  std::unique_ptr<MfemMeshManager> mesh_manager = std::make_unique<MfemMeshManager>();
  mesh_manager->load_file("brick.exo");

  mesh_manager->init();
  REQUIRE(mesh_manager->num_volume_elements() == 8790);

  // At time of writing, brick.exo does not have sidesets labelled, so we just check
  // each of the elements
  // for (int i=0; i<mesh_manager->num_boundary_elements(); i++) 
  //   REQUIRE( mesh_manager->get_surface_element_type(i) == SurfaceElementType::TRI );

}

// next, emulate the Find Element Method
TEST_CASE("TEST MFEM Find Element Method")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);

  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();

  size_t num_elements = mesh_manager->num_volume_elements();
  REQUIRE(num_elements == 10333);

  // check the overload for volume index works too
  REQUIRE(mesh_manager->num_volume_elements(1) == 10333);

  xdg->prepare_raytracer();

  MeshID volume = 1;
  MeshID element = xdg->find_element(volume, {0.0, 0.0, 100.0});
  REQUIRE(element == ID_NONE); // should not find an element since the point is outside the volume

  element = xdg->find_element(volume, {0.0, 0.0, 0.0});
  REQUIRE(element != ID_NONE); // should find an element

  auto next_element = xdg->mesh_manager()->next_element(element, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
  REQUIRE(next_element.first != ID_NONE);
  REQUIRE(next_element.second != INFTY);
}

TEST_CASE("Test Ray Fire Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  int n_rays {1000};

  for (int i = 0; i < n_rays; i++) {
    direction = rand_dir();
    std::pair<double, MeshID> intersection;
    intersection = xdg->ray_fire(volume, origin, direction);
    if (intersection.second == ID_NONE)
      fatal_error("Ray did not intersect any geometry");
    if (intersection.first > 6.4) {
      fatal_error("Ray intersected geometry at distance greater than 6.4 cm");
    }
    REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(6.3849, 1e-1));
  }
}

TEST_CASE("Test Cylinder-Brick Initialization")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<MfemMeshManager>()};

  mesh_manager->load_file("cyl-brick.exo");

  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 3);

  REQUIRE(mesh_manager->num_surfaces() == 12);

  // get an element from each volume and check its volume ID
  auto vol1_elems = mesh_manager->get_volume_elements(1);
  REQUIRE(!vol1_elems.empty());

  auto vol2_elems = mesh_manager->get_volume_elements(2);
  REQUIRE(!vol2_elems.empty());

  mesh_manager->parse_metadata();

  xdg::Property prop;

  // check the model's metadata
  prop = mesh_manager->get_volume_property(1, PropertyType::MATERIAL);
  REQUIRE(prop.type == PropertyType::MATERIAL);
  REQUIRE(prop.value == "steel");

  prop = mesh_manager->get_volume_property(2, PropertyType::MATERIAL);
  REQUIRE(prop.type == PropertyType::MATERIAL);
  REQUIRE(prop.value == "iron");

  for (auto s : mesh_manager->surfaces()) {
    prop = mesh_manager->get_surface_property(s, PropertyType::BOUNDARY_CONDITION);
    std::cout << s << ", " << prop.value << std::endl;
    REQUIRE(prop.type == PropertyType::BOUNDARY_CONDITION);
    if (s == 3) {
      REQUIRE(prop.value == "transmission");
    } else if (s == 4) {
      REQUIRE(prop.value == "reflective");
    } else {
      REQUIRE(prop.value == "vacuum");
    }
  }
}

TEST_CASE("Test Brick w/ Sidesets")
{
  std::unique_ptr<MeshManager> mesh_manager  {std::make_unique<MfemMeshManager>()};
  mesh_manager->load_file("brick-sidesets.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);
}

TEST_CASE("Test BVH Build Brick")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MfemMeshManager>();

  mesh_manager->load_file("brick.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 1);

  std::unique_ptr<RayTracer> ray_tracing_interface = std::make_unique<EmbreeRayTracer>();
  for (auto volume : mesh_manager->volumes()) {
    ray_tracing_interface->register_volume(mesh_manager, volume);
  }

  // volume elements will be detected on the mfem mesh, so three trees will be registered
  REQUIRE(ray_tracing_interface->num_registered_trees() == 3);
}


TEST_CASE("Test BVH Build Brick w/ Sidesets")
{
  std::shared_ptr<MeshManager> mesh_manager = std::make_shared<MfemMeshManager>();
  mesh_manager->load_file("brick-sidesets.exo");
  mesh_manager->init();

  REQUIRE(mesh_manager->num_volumes() == 2);
  REQUIRE(mesh_manager->num_surfaces() == 6);

  std::unique_ptr<RayTracer> ray_tracing_interface = std::make_unique<EmbreeRayTracer>();

  for (auto volume : mesh_manager->volumes()) {
    ray_tracing_interface->register_volume(mesh_manager, volume);
  }
  // volume elements will be detected on the mfem mesh, so two trees will be registered
  REQUIRE(ray_tracing_interface->num_registered_trees() == 3);
}


TEST_CASE("Test Ray Fire Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};
  std::pair<double, MeshID> intersection;

  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));
}

TEST_CASE("Test Ray Fire Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 2;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 10.0};
  Direction direction {0.0, 0.0, 1.0};
  std::pair<double, MeshID> intersection;
  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire ray in the opposite direction
  direction = {0.0, 0.0, -1.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  // this cube is 10 cm on a side, so the ray should hit the surface at 5 cm
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire ray from the center of the cylinder in the negative z direction
  volume = 1;
  origin = {0.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire a ray from the center of the cylinder in the positive z direction
  direction = {0.0, 0.0, 1.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-6));

  // fire a ray from the center of the cylinder in the positive x direction
  direction = {1.0, 0.0, 0.0};
  intersection = xdg->ray_fire(volume, origin, direction);
  REQUIRE_THAT(intersection.first, Catch::Matchers::WithinAbs(5.0, 1e-3));

  volume = 1;
  origin = {0.0, 0.0, 0.0};
  REQUIRE(xdg->point_in_volume(volume, origin));

  volume = 2;
  origin = {0.0, 0.0, 10.0};
  REQUIRE(xdg->point_in_volume(volume, origin));
}

TEST_CASE("Test Volume Element Count Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  auto elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 10333);
  REQUIRE(mesh_manager->num_volume_elements() == 10333);
}

TEST_CASE("Test Point Location Jezebel")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("jezebel.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  // the origin of the problem should be in the volume
  MeshID volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == volume);

  // a point outside of the sphere should be in the implicit complement
  origin = {0.0, 0.0, 10.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == xdg->mesh_manager()->implicit_complement());
}

TEST_CASE("Test Point Location Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  REQUIRE(mesh_manager->num_volume_elements() == 16624);

  MeshID expected_volume = 1;

  // fire ray from the center of the cube
  Position origin {0.0, 0.0, 0.0};
  Direction direction {0.0, 0.0, 1.0};

  // test a point inside the cylinder
  MeshID volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == expected_volume);

  // test a point inside the cube
  expected_volume = 2;
  origin = {0.0, 0.0, 10.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == expected_volume);

  // a point outside of the sphere should be in the implicit complement
  origin = {0.0, 0.0, 100.0};
  volume_id = xdg->find_volume(origin, direction);
  REQUIRE(volume_id == xdg->mesh_manager()->implicit_complement());
}

TEST_CASE("Test Volume Element Count Cylinder-Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("cyl-brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;
  auto elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 7587);
  auto num_volume_elements = mesh_manager->num_volume_elements(volume);
  REQUIRE(num_volume_elements == 7587);

  volume = 2;
  elements = mesh_manager->get_volume_elements(volume);
  REQUIRE(elements.size() == 9037);
  num_volume_elements = mesh_manager->num_volume_elements(volume);
  REQUIRE(num_volume_elements == 9037);
}

TEST_CASE("Test Find Element Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  REQUIRE(mesh_manager->num_volume_elements() == 8790);

  MeshID volume = 1;

  MeshID element = xdg->find_element(volume, {0.0, 0.0, 0.0});
  REQUIRE(element != ID_NONE);

  element = xdg->find_element(volume, {0.0, 0.0, 100.0});
  REQUIRE(element == ID_NONE);
}

TEST_CASE("Test Track Exiting Mesh Brick")
{
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  xdg->mesh_manager()->mesh_library();
  REQUIRE(xdg->mesh_manager()->mesh_library() == MeshLibrary::MFEM);
  const auto& mesh_manager = xdg->mesh_manager();
  mesh_manager->load_file("brick.exo");
  mesh_manager->init();
  xdg->prepare_raytracer();

  MeshID volume = 1;
  Position start {0.0, 0.0, -1000.0};
  Position end {0.0, 0.0, 1000.0};
  auto tracks = xdg->segments(volume, start, end);

  double length = std::accumulate(tracks.begin(), tracks.end(), 0.0, [](double sum, const auto& track) {
    return sum + track.second;
  });

  REQUIRE_THAT(length, Catch::Matchers::WithinAbs(10.0, 1e-6));
}

// Helpers for the implicit boundary tests below

// Average of a set of vertices
Position centroid(const std::vector<Vertex>& vertices) {
  Position sum {0.0, 0.0, 0.0};
  for (const auto& v : vertices) sum += v;
  return sum / static_cast<double>(vertices.size());
}

// Every face of a surface should be wound so that its normal points out of
// the surface's forward volume, i.e. away from the element on that side.
// Returns the number of faces that break this rule.
int count_misoriented_faces(const MfemMeshManager& mesh_manager) {
  const mfem::Mesh* mesh = mesh_manager.mesh();
  int misoriented = 0;
  for (auto surface : mesh_manager.surfaces()) {
    const MeshID forward_volume = mesh_manager.surface_senses(surface).first;
    for (auto face : mesh_manager.get_surface_faces(surface)) {
      const MeshID element = mesh_manager.get_boundary_face_element(face);
      if (element == ID_NONE || mesh->GetAttribute(element) != forward_volume) {
        misoriented++;
        continue;
      }
      const auto face_coords = mesh_manager.face_vertex_coordinates(face);
      const Direction normal = triangle_normal(face_coords);
      const Direction outward = centroid(face_coords) - centroid(mesh_manager.element_vertices(element));
      if (normal.dot(outward) <= 0.0) misoriented++;
    }
  }
  return misoriented;
}

// The volume enclosed by a volume's surfaces is only right if the surface
// senses and face windings agree, so compare it against the sum of the
// element volumes for every volume in the mesh.
void check_enclosed_volumes(const std::shared_ptr<XDG>& xdg) {
  const auto& mesh_manager = xdg->mesh_manager();
  for (auto volume : mesh_manager->volumes()) {
    if (volume == mesh_manager->implicit_complement()) continue;
    double element_volume_sum = 0.0;
    for (auto element : mesh_manager->get_volume_elements(volume))
      element_volume_sum += mesh_manager->element_volume(element);
    CAPTURE(volume);
    REQUIRE_THAT(xdg->measure_volume(volume),
                 Catch::Matchers::WithinRel(element_volume_sum, 1e-8));
  }
}

// Build a 4 x 1 x 1 bar of unit cells whose volume attribute alternates
// 1, 2, 1, 2 along x. MFEM only puts boundary elements on the exterior, so all
// three interfaces between the cells are implicit. Because the volumes
// alternate, the interface faces can't all have their Elem1 in volume 1.
std::unique_ptr<mfem::Mesh> make_alternating_bar(mfem::Element::Type type) {
  auto mesh = std::make_unique<mfem::Mesh>(
    mfem::Mesh::MakeCartesian3D(4, 1, 1, type, 4.0, 1.0, 1.0));
  mfem::Vector center;
  for (int i = 0; i < mesh->GetNE(); i++) {
    mesh->GetElementCenter(i, center);
    mesh->SetAttribute(i, static_cast<int>(center(0)) % 2 + 1);
  }
  mesh->SetAttributes();

  // MakeCartesian3D's bottom (z = 0) boundary attribute is 1 and spans both
  // volumes; its left (x = 0) boundary attribute is 5 and only touches volume 1
  mesh->bdr_attribute_sets.SetAttributeSet("boundary:reflective", mfem::Array<int>({1}));
  mesh->bdr_attribute_sets.SetAttributeSet("boundary:vacuum", mfem::Array<int>({5}));
  return mesh;
}

TEST_CASE("MFEM Implicit Boundaries Pincell")
{
  // pincell-implicit.exo only has sidesets (7-18) on the outer volume. The
  // interfaces between the inner blocks have no boundary elements at all.
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  auto mesh_manager = std::dynamic_pointer_cast<MfemMeshManager>(xdg->mesh_manager());
  REQUIRE(mesh_manager);
  mesh_manager->load_file("pincell-implicit.exo");
  mesh_manager->init();
  mesh_manager->parse_metadata();

  // four blocks plus the implicit complement
  REQUIRE(mesh_manager->num_volumes() == 5);
  // 12 explicit sidesets plus the 3 discovered interfaces
  REQUIRE(mesh_manager->num_surfaces() == 15);

  const auto& surfaces = mesh_manager->surfaces();
  auto is_sideset = [](MeshID surface) { return surface >= 7 && surface <= 18; };

  // explicit sidesets keep their IDs and metadata
  for (MeshID sideset = 7; sideset <= 18; sideset++) {
    CAPTURE(sideset);
    REQUIRE(std::find(surfaces.begin(), surfaces.end(), sideset) != surfaces.end());
    REQUIRE(mesh_manager->get_surface_property(sideset, PropertyType::BOUNDARY_CONDITION).value == "vacuum");
  }

  // the remaining surfaces are the discovered interfaces, which don't reuse
  // sideset IDs and have the default boundary condition
  std::map<std::pair<MeshID, MeshID>, MeshID> implicit_surfaces;
  for (auto surface : surfaces) {
    if (is_sideset(surface)) continue;
    implicit_surfaces[mesh_manager->surface_senses(surface)] = surface;
    REQUIRE(mesh_manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value == "transmission");
  }
  REQUIRE(implicit_surfaces.size() == 3);
  REQUIRE(mesh_manager->num_surface_faces(implicit_surfaces.at({1, 2})) == 928);
  REQUIRE(mesh_manager->num_surface_faces(implicit_surfaces.at({1, 6})) == 176);
  REQUIRE(mesh_manager->num_surface_faces(implicit_surfaces.at({2, 6})) == 1296);

  // the fuel (volume 1) is enclosed entirely by discovered surfaces
  auto fuel_surfaces = mesh_manager->get_volume_surfaces(1);
  std::sort(fuel_surfaces.begin(), fuel_surfaces.end());
  std::vector<MeshID> expected {implicit_surfaces.at({1, 2}), implicit_surfaces.at({1, 6})};
  std::sort(expected.begin(), expected.end());
  REQUIRE(fuel_surfaces == expected);

  REQUIRE(count_misoriented_faces(*mesh_manager) == 0);

  xdg->prepare_raytracer();
  check_enclosed_volumes(xdg);

  // without the discovered surfaces, no point could be located in the fuel
  Position fuel_center = mesh_manager->volume_bounding_box(1).center();
  REQUIRE(xdg->find_volume(fuel_center, {0.0, 0.0, 1.0}) == 1);
}

TEST_CASE("MFEM Multiblock sidesets")
{
  // sideset 1 covers part of the exterior of both blocks, so it is split into
  // one surface per block. The rest of each block's exterior and the
  // interface between the blocks are discovered.
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MFEM);
  auto mesh_manager = std::dynamic_pointer_cast<MfemMeshManager>(xdg->mesh_manager());
  REQUIRE(mesh_manager);
  mesh_manager->load_file("cube-w-multiblock-sideset.exo");
  mesh_manager->init();
  mesh_manager->parse_metadata();

  REQUIRE(mesh_manager->num_volumes() == 3);
  REQUIRE(mesh_manager->num_surfaces() == 5);

  // a split sideset doesn't keep its ID, as it would be ambiguous
  const auto& surfaces = mesh_manager->surfaces();
  REQUIRE(std::find(surfaces.begin(), surfaces.end(), 1) == surfaces.end());

  const MeshID ipc = mesh_manager->implicit_complement();
  std::multiset<std::pair<std::pair<MeshID, MeshID>, int>> surface_summary;
  for (auto surface : surfaces) {
    surface_summary.insert({mesh_manager->surface_senses(surface),
                            mesh_manager->num_surface_faces(surface)});
  }
  std::multiset<std::pair<std::pair<MeshID, MeshID>, int>> expected_summary {
    {{1, ipc}, 88},   // sideset 1, block 1 side
    {{1, ipc}, 1368}, // rest of block 1's exterior
    {{2, ipc}, 214},  // sideset 1, block 2 side
    {{2, ipc}, 214},  // rest of block 2's exterior
    {{1, 2}, 1048},   // interface between the blocks
  };
  // (extra parentheses: util.h's StringMaker for std::pair can't print these)
  REQUIRE((surface_summary == expected_summary));

  REQUIRE(count_misoriented_faces(*mesh_manager) == 0);

  xdg->prepare_raytracer();
  check_enclosed_volumes(xdg);

  // a track starting in block 1 should cross into block 2 and out of the mesh
  MeshID volume = 1;
  Position start {0.0, 0.0, 0.0};
  Position end {15.0, 20.0, 3.0};
  auto tracks = xdg->segments(volume, start, end);
  REQUIRE(tracks.size() > 0);
}

TEST_CASE("MFEM Implicit Boundaries With Alternating Volumes")
{
  auto type = GENERATE(mfem::Element::HEXAHEDRON, mfem::Element::TETRAHEDRON);

  DYNAMIC_SECTION("Element type: " << (type == mfem::Element::HEXAHEDRON ? "hex" : "tet")) {
    auto mesh_manager = std::make_shared<MfemMeshManager>(make_alternating_bar(type));
    mesh_manager->init();
    mesh_manager->parse_metadata();
    const mfem::Mesh* mesh = mesh_manager->mesh();
    std::shared_ptr<XDG> xdg = std::make_shared<XDG>(mesh_manager);

    // volumes 1 and 2 plus the implicit complement
    REQUIRE(mesh_manager->num_volumes() == 3);
    const MeshID ipc = mesh_manager->implicit_complement();

    // exactly one surface separates the two volumes, covering all three
    // interfaces between the cells
    std::vector<MeshID> interface_surfaces;
    for (auto surface : mesh_manager->surfaces()) {
      if (mesh_manager->surface_senses(surface) == std::pair<MeshID, MeshID>{1, 2})
        interface_surfaces.push_back(surface);
    }
    REQUIRE(interface_surfaces.size() == 1);
    const MeshID interface = interface_surfaces[0];
    REQUIRE_THAT(xdg->measure_surface_area(interface), Catch::Matchers::WithinRel(3.0, 1e-12));

    // make sure this mesh exercises reversed faces: at least one interface face
    // must have its MFEM Elem1 in volume 2
    bool has_elem1_in_volume_2 = false;
    for (auto face : mesh_manager->get_surface_faces(interface)) {
      int e1, e2;
      mesh->GetFaceElements(face, &e1, &e2);
      has_elem1_in_volume_2 |= mesh->GetAttribute(e1) == 2;
    }
    REQUIRE(has_elem1_in_volume_2);

    // ... and that every face, reversed or not, points out of volume 1
    REQUIRE(count_misoriented_faces(*mesh_manager) == 0);
    for (auto face : mesh_manager->get_surface_faces(interface)) {
      const auto coords = mesh_manager->face_vertex_coordinates(face);
      const Direction normal = triangle_normal(coords);
      const double x = centroid(coords)[0];
      // volume 1 is x in [0, 1] and [2, 3], so its outward normal is +x at
      // x = 1 and x = 3 and -x at x = 2
      const double expected_nx = std::abs(x - 2.0) < 1e-12 ? -1.0 : 1.0;
      CAPTURE(face, x);
      REQUIRE_THAT(normal[0], Catch::Matchers::WithinAbs(expected_nx, 1e-12));
    }

    // a boundary attribute spanning both volumes is split, and both pieces
    // inherit its metadata; one touching a single volume keeps its ID
    std::vector<std::pair<MeshID, MeshID>> reflective_senses;
    for (auto surface : mesh_manager->surfaces()) {
      if (mesh_manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value == "reflective")
        reflective_senses.push_back(mesh_manager->surface_senses(surface));
    }
    std::sort(reflective_senses.begin(), reflective_senses.end());
    // (extra parentheses: util.h's StringMaker for std::pair can't print these)
    REQUIRE((reflective_senses == std::vector<std::pair<MeshID, MeshID>>{{1, ipc}, {2, ipc}}));
    REQUIRE(mesh_manager->get_surface_property(5, PropertyType::BOUNDARY_CONDITION).value == "vacuum");
    REQUIRE((mesh_manager->surface_senses(5) == std::pair<MeshID, MeshID>{1, ipc}));

    xdg->prepare_raytracer();
    check_enclosed_volumes(xdg);

    // rays leaving volume 1 must stop at the interface on either side,
    // including through the reversed faces at x = 2
    Position origin {2.5, 0.3, 0.6};
    auto hit = xdg->ray_fire(1, origin, {-1.0, 0.0, 0.0});
    REQUIRE(hit.second == interface);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(0.5, 1e-10));
    hit = xdg->ray_fire(1, origin, {1.0, 0.0, 0.0});
    REQUIRE(hit.second == interface);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(0.5, 1e-10));

    // and the same from inside volume 2
    origin = {1.5, 0.3, 0.6};
    hit = xdg->ray_fire(2, origin, {1.0, 0.0, 0.0});
    REQUIRE(hit.second == interface);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(0.5, 1e-10));

    for (double x : {0.5, 2.5}) REQUIRE(xdg->find_volume({x, 0.3, 0.6}, {0.0, 0.0, 1.0}) == 1);
    for (double x : {1.5, 3.5}) REQUIRE(xdg->find_volume({x, 0.3, 0.6}, {0.0, 0.0, 1.0}) == 2);

    // walking elements along the bar crosses every interface
    auto element = xdg->find_element(1, {0.5, 0.3, 0.6});
    REQUIRE(element != ID_NONE);
    auto walk = mesh_manager->walk_elements(element, {0.5, 0.3, 0.6}, {3.5, 0.3, 0.6});
    double length = 0.0;
    for (const auto& [elem, distance] : walk) length += distance;
    REQUIRE_THAT(length, Catch::Matchers::WithinAbs(3.0, 1e-10));
  }
}
