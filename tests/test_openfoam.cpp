// stl includes
#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/openfoam/cell_shape.h"
#include "xdg/openfoam/mesh_manager.h"
#include "xdg/xdg.h"

#include "openfoam_fixtures.h"
#include "util.h"

using namespace xdg;
using namespace xdg::test::openfoam;

namespace {

//! Load a mesh manager over a fixture directory and initialize it
std::shared_ptr<OpenFOAMMeshManager> load(const TempMesh& fixture)
{
  auto mesh_manager = std::make_shared<OpenFOAMMeshManager>();
  mesh_manager->load_file(fixture.path());
  mesh_manager->init();
  mesh_manager->parse_metadata();
  return mesh_manager;
}

//! Capture what XDG writes to standard output, such as its notes, while in scope
class CaptureStdout {
public:
  CaptureStdout() : previous_(std::cout.rdbuf(captured_.rdbuf())) {}

  ~CaptureStdout() { std::cout.rdbuf(previous_); }

  CaptureStdout(const CaptureStdout&) = delete;
  CaptureStdout& operator=(const CaptureStdout&) = delete;

  //! The captured text, with XDG's line wrapping undone so that a phrase can
  //! be searched for wherever the lines happened to break
  std::string text() const
  {
    std::string out;
    bool space = false;
    for (const char c : captured_.str()) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        space = true;
        continue;
      }
      if (space && !out.empty()) out += ' ';
      space = false;
      out += c;
    }
    return out;
  }

private:
  std::ostringstream captured_;
  std::streambuf* previous_;
};

//! Centroid of an element, averaging the vertices of the faces that bound it
Position element_centroid(const OpenFOAMMeshManager& manager, MeshID element)
{
  const PolyMesh& mesh = manager.poly_mesh();
  Position centroid {0.0, 0.0, 0.0};
  int count = 0;
  for (int32_t i = 0; i < manager.num_cell_faces(element); i++) {
    const int32_t face = manager.cell_faces(element)[i];
    for (int32_t j = 0; j < mesh.face_size(face); j++) {
      centroid = centroid + mesh.points[mesh.face_vertices(face)[j]];
      count++;
    }
  }
  return centroid / static_cast<double>(count);
}

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

TEST_CASE("OpenFOAM mesh manager reports the mesh it loaded")
{
  TempMesh fixture("mm_basic");
  StructuredMesh(3, 2, 2).write(fixture);
  const auto manager = load(fixture);

  REQUIRE(manager->mesh_library() == MeshLibrary::OPENFOAM);
  REQUIRE(manager->num_volume_elements() == 12);
  REQUIRE(manager->num_vertices() == 4 * 3 * 3);

  // one volume for the mesh plus the implicit complement
  REQUIRE(manager->num_volumes() == 2);
  REQUIRE(manager->num_ents_of_dimension(3) == manager->num_volumes());
  REQUIRE(manager->num_ents_of_dimension(2) == manager->num_surfaces());
  REQUIRE(manager->num_ents_of_dimension(1) == 0);
}

TEST_CASE("The implicit complement takes the outward side of every boundary surface")
{
  TempMesh fixture("mm_ipc");
  StructuredMesh(2, 2, 2).write(fixture);
  const auto manager = load(fixture);

  const MeshID ipc = manager->implicit_complement();
  REQUIRE(ipc != ID_NONE);

  // the six box faces, all facing the complement
  REQUIRE(manager->get_volume_surfaces(ipc).size() == 6);
  for (const MeshID surface : manager->get_volume_surfaces(ipc)) {
    REQUIRE(manager->surface_senses(surface).second == ipc);
  }

  // and it holds no elements of its own
  REQUIRE(manager->num_volume_elements(ipc) == 0);
  REQUIRE(manager->get_volume_elements(ipc).empty());
  REQUIRE(manager->get_volume_element_type(ipc) == VolumeElementType::UNKNOWN);
}

TEST_CASE("cellZones become volumes carrying their material")
{
  TempMesh fixture("mm_zones");
  StructuredMesh(4, 2, 2).write(fixture, /*split_zones=*/true);
  const auto manager = load(fixture);

  // fuel, clad, and the implicit complement
  REQUIRE(manager->num_volumes() == 3);
  REQUIRE(manager->get_volume_property(1, PropertyType::MATERIAL).value == "fuel");
  REQUIRE(manager->get_volume_property(2, PropertyType::MATERIAL).value == "clad");
  REQUIRE(manager->get_volume_property(manager->implicit_complement(),
                                       PropertyType::MATERIAL).value == "void");

  REQUIRE(manager->num_volume_elements(1) == 8);
  REQUIRE(manager->num_volume_elements(2) == 8);

  // six faces per hexahedron, and none for the implicit complement
  REQUIRE(manager->num_volume_element_faces(1) == 48);
  REQUIRE(manager->num_volume_element_faces(2) == 48);
  REQUIRE(manager->num_volume_element_faces() == 96);
}

TEST_CASE("An empty cellZone is left out rather than becoming a volume")
{
  TempMesh fixture("mm_empty_zone");
  write_binary_mesh(fixture, 32);
  fixture.write("cellZones", binary_cell_zones_with_empty());
  const auto manager = load(fixture);

  // fuel, the unzoned remainder, and the implicit complement
  REQUIRE(manager->num_volumes() == 3);
  REQUIRE(manager->get_volume_property(1, PropertyType::MATERIAL).value == "fuel");
  REQUIRE(manager->num_volume_elements(1) == 1);
  for (const auto& volume : manager->topology().volumes) {
    REQUIRE(volume.name != "empty");
  }
}

TEST_CASE("Only a boundary: patch name sets a boundary condition")
{
  // CFD patch names label the model rather than state a transport condition,
  // so a plain name leaves the surface as transmission
  TempMesh fixture("mm_bcs");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"boundary:vacuum", "patch"}, {"outlet", "patch"}, {"ymin", "wall"},
     {"ymax", "patch"}, {"zmin", "patch"}, {"defaultFaces", "patch"}});
  const auto manager = load(fixture);

  std::map<std::string, std::string> condition_of_patch;
  for (const MeshID surface : manager->surfaces()) {
    const auto& patches = manager->poly_mesh().patches;
    const auto& topology_surface = manager->topology().surface(surface);
    condition_of_patch[patches[topology_surface.patch_index].name] =
      manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value;
  }

  REQUIRE(condition_of_patch["boundary:vacuum"] == "vacuum");
  for (const char* name : {"outlet", "ymin", "ymax", "zmin", "defaultFaces"}) {
    CAPTURE(name);
    REQUIRE(condition_of_patch[name] == "transmission");
  }
}

TEST_CASE("A boundary: patch group sets a boundary condition")
{
  // a group is added in the boundary file alone, unlike a rename
  TempMesh fixture("mm_bc_group");
  write_ascii_mesh(fixture);
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") +
    "3\n(\n"
    "    inlet  { type patch; nFaces 1; startFace 1; }\n"
    "    outlet { type patch; inGroups 1(boundary:reflective); nFaces 1; startFace 2; }\n"
    "    walls  { type wall; inGroups List<word> 2(wall boundary:vacuum); "
    "nFaces 8; startFace 3; }\n"
    ")\n");
  const auto manager = load(fixture);

  std::map<std::string, std::string> condition_of_patch;
  for (const MeshID surface : manager->surfaces()) {
    const auto& patches = manager->poly_mesh().patches;
    const auto& topology_surface = manager->topology().surface(surface);
    condition_of_patch[patches[topology_surface.patch_index].name] =
      manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value;
  }
  REQUIRE(condition_of_patch["walls"] == "vacuum");
  REQUIRE(condition_of_patch["outlet"] == "reflective");
  REQUIRE(condition_of_patch["inlet"] == "transmission");
  REQUIRE(manager->poly_mesh().patches[2].groups ==
          std::vector<std::string>{"wall", "boundary:vacuum"});
}

TEST_CASE("A symmetry patch becomes a reflecting boundary")
{
  TempMesh fixture("mm_symmetry");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"mirror", "symmetryPlane"}, {"side", "symmetry"}, {"open", "patch"},
     {"ymax", "patch"}, {"zmin", "wall"}, {"zmax", "patch"}});
  std::shared_ptr<OpenFOAMMeshManager> manager;
  std::string notes;
  {
    CaptureStdout capture;
    manager = load(fixture);
    notes = capture.text();
  }

  std::map<std::string, std::string> condition_of_patch;
  for (const MeshID surface : manager->surfaces()) {
    const auto& patches = manager->poly_mesh().patches;
    const auto& topology_surface = manager->topology().surface(surface);
    condition_of_patch[patches[topology_surface.patch_index].name] =
      manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value;
  }

  // a symmetry plane in CFD is a reflecting boundary for transport
  REQUIRE(condition_of_patch["mirror"] == "reflecting");
  REQUIRE(condition_of_patch["side"] == "reflecting");

  // other types, walls included, set no condition of their own
  REQUIRE(condition_of_patch["open"] == "transmission");
  REQUIRE(condition_of_patch["zmin"] == "transmission");

  // the inference is announced, naming exactly the patches it applied to, since
  // it is wrong when the neutronics model is larger than the CFD model
  CAPTURE(notes);
  REQUIRE(notes.find("symmetry type") != std::string::npos);
  REQUIRE(notes.find("'mirror'") != std::string::npos);
  REQUIRE(notes.find("'side'") != std::string::npos);
  REQUIRE(notes.find("'open'") == std::string::npos);
  REQUIRE(notes.find("boundary:<condition>") != std::string::npos);
}

TEST_CASE("A two-dimensional case loads with reflecting boundaries")
{
  // an OpenFOAM 2D case is one cell thick with `empty` front and back. The
  // geometry is invariant along that direction, so reflecting boundaries there
  // reproduce the infinite extent the case stands for -- the same idiom a 2D
  // neutronics model uses.
  TempMesh fixture("mm_2d");
  StructuredMesh(4, 4, 1, 4.0, 4.0, 0.1).write(fixture, /*split_zones=*/false,
    {{"xmin", "patch"}, {"xmax", "patch"}, {"ymin", "patch"},
     {"ymax", "patch"}, {"frontAndBack", "empty"}, {"frontAndBack", "empty"}});
  const auto manager = load(fixture);

  REQUIRE(manager->num_volume_elements() == 16);

  std::map<std::string, std::string> condition_of_patch;
  for (const MeshID surface : manager->surfaces()) {
    const auto& patches = manager->poly_mesh().patches;
    const auto& topology_surface = manager->topology().surface(surface);
    condition_of_patch[patches[topology_surface.patch_index].name] =
      manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value;
  }

  REQUIRE(condition_of_patch["frontAndBack"] == "reflecting");
  REQUIRE(condition_of_patch["xmin"] == "transmission");
}

TEST_CASE("An empty patch on a mesh more than one cell thick is not read as 2D")
{
  // blockMesh gives type empty to its default patch, so a 3D mesh whose
  // boundary list was left out has empty patches all round
  TempMesh fixture("mm_not_2d");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"xmin", "empty"}, {"xmax", "empty"}, {"ymin", "empty"},
     {"ymax", "empty"}, {"zmin", "empty"}, {"zmax", "empty"}});
  const auto manager = load(fixture);

  for (const MeshID surface : manager->surfaces()) {
    REQUIRE(manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value ==
            "transmission");
  }
}

TEST_CASE("An explicit boundary: name overrides the patch type")
{
  TempMesh fixture("mm_explicit_bc");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"boundary:vacuum", "symmetryPlane"}, {"boundary:reflective", "patch"},
     {"ymin", "patch"}, {"ymax", "patch"}, {"zmin", "patch"}, {"zmax", "patch"}});
  std::shared_ptr<OpenFOAMMeshManager> manager;
  std::string notes;
  {
    CaptureStdout capture;
    manager = load(fixture);
    notes = capture.text();
  }

  std::map<std::string, std::string> condition_of_patch;
  for (const MeshID surface : manager->surfaces()) {
    const auto& patches = manager->poly_mesh().patches;
    const auto& topology_surface = manager->topology().surface(surface);
    condition_of_patch[patches[topology_surface.patch_index].name] =
      manager->get_surface_property(surface, PropertyType::BOUNDARY_CONDITION).value;
  }

  // the stated condition wins over what the type would have implied, and
  // nothing having been inferred, there is nothing to announce
  REQUIRE(condition_of_patch["boundary:vacuum"] == "vacuum");
  REQUIRE(condition_of_patch["boundary:reflective"] == "reflective");
  CAPTURE(notes);
  REQUIRE(notes.find("symmetry type") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Element shape and connectivity
// ---------------------------------------------------------------------------

TEST_CASE("Hexahedral cells are recognized and ordered canonically")
{
  TempMesh fixture("mm_hex");
  StructuredMesh(2, 2, 2).write(fixture);
  const auto manager = load(fixture);

  REQUIRE(manager->get_volume_element_type(1) == VolumeElementType::HEX);
  REQUIRE(manager->get_surface_face_type(1) == SurfaceFaceType::QUAD);
  for (const MeshID face : manager->get_surface_faces(1)) {
    REQUIRE(manager->face_type(face) == SurfaceFaceType::QUAD);
  }
  REQUIRE(manager->element_type(manager->num_volume_elements()) == VolumeElementType::UNKNOWN);
  REQUIRE(manager->face_type(-1) == SurfaceFaceType::UNKNOWN);

  for (const MeshID element : manager->get_volume_elements(1)) {
    REQUIRE(manager->element_type(element) == VolumeElementType::HEX);
    const auto connectivity = manager->element_connectivity(element);
    REQUIRE(connectivity.size() == 8);
    REQUIRE(std::set<MeshID>(connectivity.begin(), connectivity.end()).size() == 8);
    REQUIRE(manager->element_vertices(element).size() == 8);
  }
}

TEST_CASE("Tetrahedral cells are recognized and ordered canonically")
{
  TempMesh fixture("mm_tet");
  write_single_tet(fixture);
  const auto manager = load(fixture);

  REQUIRE(manager->num_volume_elements() == 1);
  REQUIRE(manager->get_volume_element_type(1) == VolumeElementType::TET);
  REQUIRE(manager->num_volume_element_faces(1) == 4);
  REQUIRE(manager->get_surface_face_type(1) == SurfaceFaceType::TRI);
  REQUIRE(manager->element_type(0) == VolumeElementType::TET);
  for (const MeshID face : manager->get_surface_faces(1)) {
    REQUIRE(manager->face_type(face) == SurfaceFaceType::TRI);
  }
  REQUIRE(manager->element_connectivity(0).size() == 4);
  REQUIRE_THAT(manager->element_volume(0), Catch::Matchers::WithinAbs(1.0 / 6.0, 1e-12));
}

TEST_CASE("Canonical hex ordering reproduces the cell's own faces")
{
  TempMesh fixture("mm_hexfaces");
  StructuredMesh(2, 2, 2).write(fixture);
  const auto manager = load(fixture);
  const PolyMesh& mesh = manager->poly_mesh();

  // the face table the containment test indexes
  static const int hex_faces[6][4] = {
    {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
    {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}
  };

  for (MeshID element = 0; element < manager->num_volume_elements(); element++) {
    const auto nodes = manager->element_connectivity(element);

    std::set<std::vector<MeshID>> implied;
    for (const auto& face : hex_faces) {
      std::vector<MeshID> vertices {nodes[face[0]], nodes[face[1]],
                                    nodes[face[2]], nodes[face[3]]};
      std::sort(vertices.begin(), vertices.end());
      implied.insert(vertices);
    }

    std::set<std::vector<MeshID>> actual;
    for (int32_t i = 0; i < manager->num_cell_faces(element); i++) {
      const int32_t face = manager->cell_faces(element)[i];
      std::vector<MeshID> vertices(mesh.face_vertices(face),
                                   mesh.face_vertices(face) + mesh.face_size(face));
      std::sort(vertices.begin(), vertices.end());
      actual.insert(vertices);
    }

    REQUIRE(implied == actual);
  }
}

TEST_CASE("Element volumes sum to the volume of the box")
{
  TempMesh fixture("mm_volume");
  StructuredMesh(3, 2, 2, 3.0, 2.0, 2.0).write(fixture);
  const auto manager = load(fixture);

  double total = 0.0;
  for (MeshID element = 0; element < manager->num_volume_elements(); element++) {
    total += manager->element_volume(element);
  }
  REQUIRE_THAT(total, Catch::Matchers::WithinAbs(12.0, 1e-9));
}

// ---------------------------------------------------------------------------
// Adjacency and face orientation
// ---------------------------------------------------------------------------

TEST_CASE("Adjacency is symmetric across shared faces")
{
  TempMesh fixture("mm_adjacency");
  StructuredMesh(3, 2, 2).write(fixture);
  const auto manager = load(fixture);

  int interior_links = 0;
  for (MeshID element = 0; element < manager->num_volume_elements(); element++) {
    for (int32_t i = 0; i < manager->num_cell_faces(element); i++) {
      const MeshID neighbor = manager->adjacent_element(element, i);
      if (neighbor == ID_NONE) continue;
      interior_links++;

      // the neighbor must name this element back across the same face
      bool reciprocated = false;
      for (int32_t j = 0; j < manager->num_cell_faces(neighbor); j++) {
        if (manager->adjacent_element(neighbor, j) == element) reciprocated = true;
      }
      REQUIRE(reciprocated);
    }
  }

  // 12 cells in a 3x2x2 grid share 20 internal faces, seen from both sides
  REQUIRE(interior_links == 40);
}

TEST_CASE("Element face vertices are wound to point out of the element")
{
  TempMesh fixture("mm_facing");
  StructuredMesh(2, 2, 2).write(fixture);
  const auto manager = load(fixture);

  for (MeshID element = 0; element < manager->num_volume_elements(); element++) {
    const auto accessor = ElementFaceAccessor::create(manager.get(), element);
    REQUIRE(accessor->num_faces() == 6);

    const Position centroid = element_centroid(*manager, element);
    for (int i = 0; i < accessor->num_faces(); i++) {
      const auto vertices = accessor->face_vertices(i);
      REQUIRE(vertices.size() == 4);

      const Direction normal =
        (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);

      Position face_centroid {0.0, 0.0, 0.0};
      for (const auto& vertex : vertices) face_centroid = face_centroid + vertex;
      face_centroid = face_centroid / static_cast<double>(vertices.size());

      REQUIRE(normal.dot(face_centroid - centroid) > 0.0);
    }
  }
}

TEST_CASE("A boundary face resolves to the element behind it")
{
  TempMesh fixture("mm_boundaryface");
  StructuredMesh(2, 2, 2).write(fixture);
  const auto manager = load(fixture);
  const PolyMesh& mesh = manager->poly_mesh();

  for (const MeshID surface : manager->surfaces()) {
    for (const MeshID face : manager->get_surface_faces(surface)) {
      const MeshID element = manager->get_boundary_face_element(face);
      REQUIRE(element != ID_NONE);

      // that element must actually be bounded by the face
      bool bounded = false;
      for (int32_t i = 0; i < manager->num_cell_faces(element); i++) {
        if (manager->cell_faces(element)[i] == face) bounded = true;
      }
      REQUIRE(bounded);
      REQUIRE(mesh.face_size(face) == 4);
    }
  }
}

// ---------------------------------------------------------------------------
// Geometric queries through the full XDG interface
// ---------------------------------------------------------------------------

TEST_CASE("Point containment through XDG")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_piv");
  StructuredMesh(2, 2, 2, 2.0, 2.0, 2.0).write(fixture);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  REQUIRE(xdg->point_in_volume(1, {1.0, 1.0, 1.0}));
  REQUIRE(xdg->point_in_volume(1, {0.1, 0.1, 0.1}));
  REQUIRE_FALSE(xdg->point_in_volume(1, {3.0, 1.0, 1.0}));
  REQUIRE_FALSE(xdg->point_in_volume(1, {-0.5, 1.0, 1.0}));
}

TEST_CASE("Ray fire returns the distance to the box wall")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_rayfire");
  StructuredMesh(2, 2, 2, 2.0, 2.0, 2.0).write(fixture);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  // from the center of a two-by-two-by-two box, every wall is one away
  const Position center {1.0, 1.0, 1.0};
  const std::vector<Direction> directions = {
    {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
    {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}
  };

  for (const auto& direction : directions) {
    const auto hit = xdg->ray_fire(1, center, direction);
    REQUIRE(hit.second != ID_NONE);
    REQUIRE_THAT(hit.first, Catch::Matchers::WithinAbs(1.0, 1e-9));
  }
}

TEST_CASE("Measured volume and surface area match the box")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_measure");
  StructuredMesh(2, 3, 2, 2.0, 3.0, 2.0).write(fixture);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  REQUIRE_THAT(xdg->measure_volume(1), Catch::Matchers::WithinAbs(12.0, 1e-9));

  double area = 0.0;
  for (const MeshID surface : xdg->mesh_manager()->surfaces()) {
    area += xdg->measure_surface_area(surface);
  }
  // two faces of each of 2x3, 3x2 and 2x2
  REQUIRE_THAT(area, Catch::Matchers::WithinAbs(2 * (6.0 + 4.0 + 6.0), 1e-9));
}

TEST_CASE("Walking elements crosses the mesh and accounts for the whole track")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_walk");
  StructuredMesh(4, 1, 1, 4.0, 1.0, 1.0).write(fixture);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  // straight down the length of a four-by-one-by-one bar of unit cells
  const auto segments = xdg->segments({0.0, 0.5, 0.5}, {4.0, 0.5, 0.5});

  double total = 0.0;
  for (const auto& [element, length] : segments) total += length;
  REQUIRE_THAT(total, Catch::Matchers::WithinAbs(4.0, 1e-9));

  // and it should pass through all four cells, one unit in each
  std::set<MeshID> visited;
  for (const auto& [element, length] : segments) visited.insert(element);
  REQUIRE(visited.size() == 4);
}

TEST_CASE("A track across a cellZone interface is split between the volumes")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_interface");
  StructuredMesh(4, 1, 1, 4.0, 1.0, 1.0).write(fixture, /*split_zones=*/true);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  const auto manager = xdg->mesh_manager();
  REQUIRE(manager->num_volume_elements(1) == 2);
  REQUIRE(manager->num_volume_elements(2) == 2);

  std::map<MeshID, MeshID> volume_of_element;
  for (const MeshID volume : {1, 2}) {
    for (const MeshID element : manager->get_volume_elements(volume)) {
      volume_of_element[element] = volume;
    }
  }

  // element traversal follows adjacency rather than stopping at a volume
  // boundary, so the track is attributed to volumes after the fact
  double fuel = 0.0;
  double clad = 0.0;
  for (const auto& [element, length] :
       xdg->segments({0.0, 0.5, 0.5}, {4.0, 0.5, 0.5})) {
    (volume_of_element.at(element) == 1 ? fuel : clad) += length;
  }

  // the interface is a full plane at the midpoint, so each volume takes half
  REQUIRE_THAT(fuel, Catch::Matchers::WithinAbs(2.0, 1e-9));
  REQUIRE_THAT(clad, Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("Finding the volume containing a point")
{
  check_mesh_library_supported(MeshLibrary::OPENFOAM);
  check_ray_tracer_supported(RTLibrary::EMBREE);

  TempMesh fixture("xdg_findvol");
  StructuredMesh(4, 1, 1, 4.0, 1.0, 1.0).write(fixture, /*split_zones=*/true);

  auto xdg = XDG::create(MeshLibrary::OPENFOAM, RTLibrary::EMBREE);
  xdg->mesh_manager()->load_file(fixture.path());
  xdg->mesh_manager()->init();
  xdg->mesh_manager()->parse_metadata();
  xdg->prepare_raytracer();

  const Direction direction {1.0, 0.0, 0.0};
  REQUIRE(xdg->find_volume({1.0, 0.5, 0.5}, direction) == 1);
  REQUIRE(xdg->find_volume({3.0, 0.5, 0.5}, direction) == 2);
}
