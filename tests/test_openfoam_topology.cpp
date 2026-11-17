// stl includes
#include <algorithm>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/openfoam/poly_mesh.h"
#include "xdg/openfoam/poly_mesh_topology.h"

#include "openfoam_fixtures.h"

using namespace xdg;
using namespace xdg::test::openfoam;

namespace {

using Zone = std::pair<std::string, std::vector<int>>;

//! Build a cellZones file assigning the named cells to the named zones
std::string cell_zones(const std::vector<Zone>& zones)
{
  std::string text = foam_header("regIOobject", "cellZones");
  text += std::to_string(zones.size()) + "\n(";
  for (const auto& [name, cells] : zones) {
    text += name + "\n{\n    type            cellZone;\n    cellLabels      List<label> " +
            std::to_string(cells.size()) + "(";
    for (size_t i = 0; i < cells.size(); i++) {
      if (i > 0) text += " ";
      text += std::to_string(cells[i]);
    }
    text += ");\n}\n";
  }
  text += ")\n";
  return text;
}

//! Read the canonical mesh, optionally with cellZones, and derive its topology
PolyMeshTopology topology_of(const TempMesh& fixture,
                             const std::vector<Zone>& zones = {})
{
  write_ascii_mesh(fixture);
  if (!zones.empty()) fixture.write("cellZones", cell_zones(zones));
  return build_topology(read_poly_mesh(fixture.path()));
}

//! Face indices belonging to the surface named \p name
std::vector<int32_t> faces_of(const PolyMeshTopology& topology, const std::string& name)
{
  for (const auto& surface : topology.surfaces) {
    if (surface.name == name) return surface.faces;
  }
  return {};
}

const PolyMeshSurface* surface_named(const PolyMeshTopology& topology,
                                     const std::string& name)
{
  for (const auto& surface : topology.surfaces) {
    if (surface.name == name) return &surface;
  }
  return nullptr;
}

//! The single interior interface surface, if there is one
const PolyMeshSurface* interface_surface(const PolyMeshTopology& topology)
{
  for (const auto& surface : topology.surfaces) {
    if (!surface.is_boundary()) return &surface;
  }
  return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Cell to face inversion
// ---------------------------------------------------------------------------

TEST_CASE("Cells are bounded by the faces that name them")
{
  TempMesh fixture("cellfaces");
  const PolyMeshTopology topology = topology_of(fixture);

  REQUIRE(topology.num_cells() == N_CELLS);

  // both cells of this mesh are hexes, so both are bounded by six faces
  REQUIRE(topology.num_cell_faces(0) == 6);
  REQUIRE(topology.num_cell_faces(1) == 6);

  const std::vector<int32_t> cell0(topology.cell_face_list(0),
                                   topology.cell_face_list(0) + 6);
  const std::vector<int32_t> cell1(topology.cell_face_list(1),
                                   topology.cell_face_list(1) + 6);

  // owner faces for cell 0; cell 1 additionally picks up the shared face 0
  // through the neighbour list
  REQUIRE(cell0 == std::vector<int32_t>{0, 1, 3, 5, 7, 9});
  REQUIRE(cell1 == std::vector<int32_t>{0, 2, 4, 6, 8, 10});
}

TEST_CASE("A cell's face run is sorted, fixing the local face index")
{
  TempMesh fixture("cellfaceorder");
  const PolyMeshTopology topology = topology_of(fixture);

  for (int32_t c = 0; c < topology.num_cells(); c++) {
    const int32_t* faces = topology.cell_face_list(c);
    const int32_t n = topology.num_cell_faces(c);
    REQUIRE(std::is_sorted(faces, faces + n));
  }
}

TEST_CASE("Every face is referenced once per cell it bounds")
{
  TempMesh fixture("facerefs");
  const PolyMeshTopology topology = topology_of(fixture);

  std::vector<int> references(N_FACES, 0);
  for (const int32_t face : topology.cell_faces) references[face]++;

  // an internal face bounds two cells, a boundary face only one
  REQUIRE(references[0] == 2);
  for (int f = N_INTERNAL_FACES; f < N_FACES; f++) {
    REQUIRE(references[f] == 1);
  }
}

// ---------------------------------------------------------------------------
// Volumes
// ---------------------------------------------------------------------------

TEST_CASE("A mesh without cellZones becomes a single volume")
{
  TempMesh fixture("novolumes");
  const PolyMeshTopology topology = topology_of(fixture);

  REQUIRE(topology.volumes.size() == 1);
  REQUIRE(topology.volumes[0].id == 1);
  REQUIRE(topology.volumes[0].name.empty());
  REQUIRE(topology.volumes[0].cells == std::vector<int32_t>{0, 1});
  REQUIRE(topology.cell_volume == std::vector<MeshID>{1, 1});
}

TEST_CASE("Each cellZone becomes a volume")
{
  TempMesh fixture("volumes");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  REQUIRE(topology.volumes.size() == 2);
  REQUIRE(topology.volume(1).name == "fuel");
  REQUIRE(topology.volume(2).name == "clad");
  REQUIRE(topology.cell_volume == std::vector<MeshID>{1, 2});
}

TEST_CASE("A cell listed twice in one cellZone is counted once")
{
  // checkMesh only warns about a repeated label, so such a mesh must load
  TempMesh fixture("topo_repeated");
  const auto topology = topology_of(fixture, {{"fuel", {0, 0}}, {"clad", {1}}});

  REQUIRE(topology.volumes.size() == 2);
  REQUIRE(topology.volumes[0].cells == std::vector<int32_t>{0});
  REQUIRE(topology.volumes[1].cells == std::vector<int32_t>{1});
}

TEST_CASE("Cells outside every cellZone are collected into one unnamed volume")
{
  TempMesh fixture("partialzone");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}});

  REQUIRE(topology.volumes.size() == 2);
  REQUIRE(topology.volume(1).name == "fuel");
  REQUIRE(topology.volume(1).cells == std::vector<int32_t>{0});
  REQUIRE(topology.volume(2).name.empty());
  REQUIRE(topology.volume(2).cells == std::vector<int32_t>{1});
}

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

TEST_CASE("Patches become surfaces when they touch a single volume")
{
  TempMesh fixture("patchsurfaces");
  const PolyMeshTopology topology = topology_of(fixture);

  // one surface per patch, and no interface because there is only one volume
  REQUIRE(topology.surfaces.size() == 3);
  REQUIRE(faces_of(topology, "inlet") == std::vector<int32_t>{1});
  REQUIRE(faces_of(topology, "outlet") == std::vector<int32_t>{2});
  REQUIRE(faces_of(topology, "walls") ==
          std::vector<int32_t>{3, 4, 5, 6, 7, 8, 9, 10});
  REQUIRE(interface_surface(topology) == nullptr);
}

TEST_CASE("A patch straddling two volumes splits into one surface per volume")
{
  TempMesh fixture("straddle");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  // inlet, outlet, the two halves of walls, and the fuel/clad interface
  REQUIRE(topology.surfaces.size() == 5);

  // the eight wall faces alternate between the cells, so the split interleaves
  REQUIRE(faces_of(topology, "walls/fuel") == std::vector<int32_t>{3, 5, 7, 9});
  REQUIRE(faces_of(topology, "walls/clad") == std::vector<int32_t>{4, 6, 8, 10});

  // an unsplit patch keeps its bare name
  REQUIRE(surface_named(topology, "inlet") != nullptr);
  REQUIRE(surface_named(topology, "outlet") != nullptr);
}

TEST_CASE("An internal face between two volumes becomes an interface surface")
{
  TempMesh fixture("interface");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  const PolyMeshSurface* interface = interface_surface(topology);
  REQUIRE(interface != nullptr);
  REQUIRE(interface->faces == std::vector<int32_t>{0});
  REQUIRE(interface->name.empty());
  REQUIRE(interface->patch_index == -1);
}

TEST_CASE("Faces interior to a volume belong to no surface")
{
  TempMesh fixture("interior");
  const PolyMeshTopology topology = topology_of(fixture);

  // the shared face has the same volume on both sides here
  REQUIRE(topology.face_surface[0] == ID_NONE);
  for (int f = N_INTERNAL_FACES; f < N_FACES; f++) {
    REQUIRE(topology.face_surface[f] != ID_NONE);
  }
}

TEST_CASE("Surface IDs are contiguous from one and resolve back to the surface")
{
  TempMesh fixture("ids");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  for (size_t i = 0; i < topology.surfaces.size(); i++) {
    REQUIRE(topology.surfaces[i].id == static_cast<MeshID>(i + 1));
    REQUIRE(topology.surface(topology.surfaces[i].id).id == topology.surfaces[i].id);
  }
  for (size_t i = 0; i < topology.volumes.size(); i++) {
    REQUIRE(topology.volumes[i].id == static_cast<MeshID>(i + 1));
    REQUIRE(topology.volume(topology.volumes[i].id).id == topology.volumes[i].id);
  }
}

// ---------------------------------------------------------------------------
// Senses
// ---------------------------------------------------------------------------

TEST_CASE("A boundary surface faces out of its owning volume")
{
  TempMesh fixture("boundarysense");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  // inlet is owned by cell 0, which is fuel; outlet by cell 1, which is clad
  REQUIRE(surface_named(topology, "inlet")->forward == 1);
  REQUIRE(surface_named(topology, "inlet")->reverse == ID_NONE);
  REQUIRE(surface_named(topology, "outlet")->forward == 2);
  REQUIRE(surface_named(topology, "outlet")->reverse == ID_NONE);
}

TEST_CASE("An interface faces out of the lower-numbered volume")
{
  TempMesh fixture("interfacesense");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  const PolyMeshSurface* interface = interface_surface(topology);
  REQUIRE(interface->forward == 1);
  REQUIRE(interface->reverse == 2);

  // face 0 is owned by cell 0, which is in the forward volume, so its stored
  // winding already points the right way
  REQUIRE(topology.face_flipped[0] == false);
}

TEST_CASE("A face whose owner is in the reverse volume is flagged for flipping")
{
  // the zones are declared in the opposite order, so volume 1 is now the cell
  // that OpenFOAM stores as the neighbour of the shared face
  TempMesh fixture("flip");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {1}}, {"clad", {0}}});

  REQUIRE(topology.cell_volume == std::vector<MeshID>{2, 1});

  const PolyMeshSurface* interface = interface_surface(topology);
  REQUIRE(interface->forward == 1);
  REQUIRE(interface->reverse == 2);

  // face 0 is owned by cell 0, which is now the reverse volume, so its winding
  // has to be reversed for the normal to point out of the forward volume
  REQUIRE(topology.face_flipped[0] == true);
}

TEST_CASE("Boundary faces are never flipped")
{
  TempMesh fixture("noflip");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {1}}, {"clad", {0}}});

  for (int f = N_INTERNAL_FACES; f < N_FACES; f++) {
    REQUIRE(topology.face_flipped[f] == false);
  }
}

TEST_CASE("Face normals point out of the forward volume")
{
  // the zones are ordered so that the interface face needs flipping, which puts
  // both the flipped and the unflipped path under the same geometric check
  TempMesh fixture("normals");
  write_ascii_mesh(fixture);
  fixture.write("cellZones", cell_zones({{"fuel", {1}}, {"clad", {0}}}));

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  const PolyMeshTopology topology = build_topology(mesh);

  for (const auto& surface : topology.surfaces) {
    for (const int32_t face : surface.faces) {
      // the winding as the geometry will actually use it
      std::vector<Vertex> vertices;
      for (int32_t i = 0; i < mesh.face_size(face); i++) {
        vertices.push_back(mesh.points[mesh.face_vertices(face)[i]]);
      }
      if (topology.face_flipped[face]) {
        std::reverse(vertices.begin(), vertices.end());
      }

      const Vertex normal =
        (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);

      Vertex face_centroid {0.0, 0.0, 0.0};
      for (const auto& vertex : vertices) face_centroid = face_centroid + vertex;
      face_centroid = face_centroid / static_cast<double>(vertices.size());

      // the cell the normal should be pointing away from
      const int32_t cell =
        topology.face_flipped[face] ? mesh.neighbour[face] : mesh.owner[face];
      REQUIRE(topology.cell_volume[cell] == surface.forward);

      Vertex cell_centroid {0.0, 0.0, 0.0};
      int count = 0;
      for (int32_t i = 0; i < topology.num_cell_faces(cell); i++) {
        const int32_t cell_face = topology.cell_face_list(cell)[i];
        for (int32_t j = 0; j < mesh.face_size(cell_face); j++) {
          cell_centroid = cell_centroid + mesh.points[mesh.face_vertices(cell_face)[j]];
          count++;
        }
      }
      cell_centroid = cell_centroid / static_cast<double>(count);

      REQUIRE(normal.dot(face_centroid - cell_centroid) > 0.0);
    }
  }
}

// ---------------------------------------------------------------------------
// Coverage
// ---------------------------------------------------------------------------

TEST_CASE("Every face is accounted for exactly once")
{
  TempMesh fixture("coverage");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  std::vector<int> assignments(N_FACES, 0);
  for (const auto& surface : topology.surfaces) {
    for (const int32_t face : surface.faces) assignments[face]++;
  }

  for (int f = 0; f < N_FACES; f++) {
    REQUIRE(assignments[f] == 1);
    REQUIRE(topology.face_surface[f] != ID_NONE);
  }
}

TEST_CASE("face_surface agrees with the surface face lists")
{
  TempMesh fixture("agreement");
  const PolyMeshTopology topology = topology_of(fixture, {{"fuel", {0}}, {"clad", {1}}});

  for (const auto& surface : topology.surfaces) {
    for (const int32_t face : surface.faces) {
      REQUIRE(topology.face_surface[face] == surface.id);
    }
  }
}
