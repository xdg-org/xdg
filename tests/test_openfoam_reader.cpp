// stl includes
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/openfoam/foam_file.h"
#include "xdg/openfoam/foam_token_stream.h"
#include "xdg/openfoam/poly_mesh.h"

#include "openfoam_fixtures.h"

using namespace xdg;
using namespace xdg::test::openfoam;

namespace {

//! Assert that \p mesh matches the canonical two-cell mesh
void check_canonical(const PolyMesh& mesh)
{
  REQUIRE(mesh.num_points() == N_POINTS);
  REQUIRE(mesh.num_faces() == N_FACES);
  REQUIRE(mesh.n_internal_faces == N_INTERNAL_FACES);
  REQUIRE(mesh.n_cells == N_CELLS);

  for (int i = 0; i < N_POINTS; i++) {
    REQUIRE_THAT(mesh.points[i].x,
                 Catch::Matchers::WithinAbs(POINTS[i][0], 1e-12));
    REQUIRE_THAT(mesh.points[i].y,
                 Catch::Matchers::WithinAbs(POINTS[i][1], 1e-12));
    REQUIRE_THAT(mesh.points[i].z,
                 Catch::Matchers::WithinAbs(POINTS[i][2], 1e-12));
  }

  for (int f = 0; f < N_FACES; f++) {
    REQUIRE(mesh.face_size(f) == 4);
    const int32_t* vertices = mesh.face_vertices(f);
    for (int j = 0; j < 4; j++) {
      REQUIRE(vertices[j] == FACES[f][j]);
    }
  }

  REQUIRE(mesh.owner.size() == static_cast<size_t>(N_FACES));
  for (int f = 0; f < N_FACES; f++) REQUIRE(mesh.owner[f] == OWNER[f]);

  REQUIRE(mesh.neighbour.size() == static_cast<size_t>(N_INTERNAL_FACES));
  REQUIRE(mesh.neighbour[0] == NEIGHBOUR[0]);

  REQUIRE(mesh.patches.size() == 3);
  REQUIRE(mesh.patches[0].name == "inlet");
  REQUIRE(mesh.patches[0].type == "patch");
  REQUIRE(mesh.patches[0].start_face == 1);
  REQUIRE(mesh.patches[0].n_faces == 1);
  REQUIRE(mesh.patches[2].name == "walls");
  REQUIRE(mesh.patches[2].type == "wall");
  REQUIRE(mesh.patches[2].start_face == 3);
  REQUIRE(mesh.patches[2].n_faces == 8);
}

//! Remove the count in front of a file's top-level list, giving the unsized
//! spelling `( ... )` that OpenFOAM also accepts in ASCII
std::string unsized(const std::string& text)
{
  const std::string header_end = " //\n\n";
  size_t start = text.find(header_end);
  REQUIRE(start != std::string::npos);
  start += header_end.size();

  size_t stop = start;
  while (stop < text.size() && std::isdigit(static_cast<unsigned char>(text[stop]))) {
    stop++;
  }
  REQUIRE(stop > start);
  if (stop < text.size() && text[stop] == '\n') stop++;
  return text.substr(0, start) + text.substr(stop);
}

} // namespace

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

TEST_CASE("Foam tokenizer skips both comment styles")
{
  FoamTokenStream ts("/* banner\n   spanning lines */\n// line comment\nalpha ; // trailing\nbeta",
                     "memory");
  REQUIRE(ts.next() == "alpha");
  REQUIRE(ts.next() == ";");
  REQUIRE(ts.next() == "beta");
  REQUIRE(ts.eof());
}

TEST_CASE("Foam tokenizer separates punctuation from words")
{
  FoamTokenStream ts("List<label> 3(7 8 9);", "memory");
  REQUIRE(ts.next() == "List");
  REQUIRE(ts.next() == "<");
  REQUIRE(ts.next() == "label");
  REQUIRE(ts.next() == ">");
  REQUIRE(ts.next_label() == 3);
  REQUIRE(ts.next() == "(");
  REQUIRE(ts.next_label() == 7);
}

TEST_CASE("Foam tokenizer returns quoted strings without delimiters")
{
  FoamTokenStream ts("arch \"LSB;label=32;scalar=64\";", "memory");
  REQUIRE(ts.next() == "arch");
  REQUIRE(ts.next() == "LSB;label=32;scalar=64");
  REQUIRE(ts.next() == ";");
}

TEST_CASE("Foam tokenizer peek does not advance the stream")
{
  FoamTokenStream ts("one two", "memory");
  REQUIRE(ts.peek() == "one");
  REQUIRE(ts.peek() == "one");
  REQUIRE(ts.next() == "one");
  REQUIRE(ts.peek() == "two");
}

TEST_CASE("Foam tokenizer at_char probes the next delimiter without consuming it")
{
  FoamTokenStream ts("  /* comment */ ( 7 )", "memory");
  REQUIRE(ts.at_char('('));
  REQUIRE(ts.at_char('('));
  REQUIRE_FALSE(ts.at_char(')'));
  REQUIRE(ts.next() == "(");
  REQUIRE_FALSE(ts.at_char(')'));
  REQUIRE(ts.next_label() == 7);
  REQUIRE(ts.at_char(')'));
  REQUIRE(ts.next() == ")");
  REQUIRE_FALSE(ts.at_char(')'));
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

TEST_CASE("FoamFile header carries format, arch, class and counts")
{
  FoamTokenStream ts(foam_header("labelList", "owner", "binary", MESH_NOTE), "memory");
  const FoamHeader header = read_foam_header(ts);

  REQUIRE(header.format == FoamFormat::BINARY);
  REQUIRE(header.class_name == "labelList");
  REQUIRE(header.object == "owner");
  REQUIRE(header.arch.little_endian);
  REQUIRE(header.arch.label_bits == 32);
  REQUIRE(header.arch.scalar_bits == 64);
  REQUIRE(header.label_bytes() == 4);
  REQUIRE(header.scalar_bytes() == 8);

  REQUIRE(header.counts.n_points == 12);
  REQUIRE(header.counts.n_cells == 2);
  REQUIRE(header.counts.n_faces == 11);
  REQUIRE(header.counts.n_internal_faces == 1);
}

TEST_CASE("FoamFile header skips nested dictionaries such as meta")
{
  const std::string text =
    "FoamFile\n{\n    format ascii;\n    class regIOobject;\n"
    "    object cellZones;\n"
    "    meta { names 2 ( fuel clad ); }\n}\n";
  FoamTokenStream ts(text, "memory");
  const FoamHeader header = read_foam_header(ts);

  REQUIRE(header.class_name == "regIOobject");
  REQUIRE(header.object == "cellZones");
  REQUIRE(header.counts.n_cells == -1);
}

TEST_CASE("FoamFile header defaults apply when arch is absent")
{
  FoamTokenStream ts("FoamFile\n{\n    format ascii;\n    class labelList;\n}\n", "memory");
  const FoamHeader header = read_foam_header(ts);

  REQUIRE(header.arch.little_endian);
  REQUIRE(header.arch.label_bits == 32);
  REQUIRE(header.arch.scalar_bits == 64);
}

// ---------------------------------------------------------------------------
// Whole meshes
// ---------------------------------------------------------------------------

TEST_CASE("Read an ASCII polyMesh directory")
{
  TempMesh fixture("ascii");
  write_ascii_mesh(fixture);

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
  REQUIRE(mesh.cell_zones.empty());
}

TEST_CASE("Read a polyMesh through its case directory")
{
  TempMesh fixture("case");
  write_ascii_mesh(fixture, "constant/polyMesh/");

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read a binary polyMesh with a faceCompactList")
{
  TempMesh fixture("binary32");
  write_binary_mesh(fixture, 32);

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read a binary polyMesh written with 64-bit labels")
{
  TempMesh fixture("binary64");
  write_binary_mesh(fixture, 64);

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("ASCII and binary meshes parse identically")
{
  TempMesh ascii("cmp_ascii");
  TempMesh binary("cmp_binary");
  write_ascii_mesh(ascii);
  write_binary_mesh(binary, 32);

  const PolyMesh a = read_poly_mesh(ascii.path());
  const PolyMesh b = read_poly_mesh(binary.path());

  REQUIRE(a.face_offsets == b.face_offsets);
  REQUIRE(a.face_points == b.face_points);
  REQUIRE(a.owner == b.owner);
  REQUIRE(a.neighbour == b.neighbour);
  REQUIRE(a.n_cells == b.n_cells);
  REQUIRE(a.n_internal_faces == b.n_internal_faces);
}

TEST_CASE("Read cellZones, including the typed List<label> spelling")
{
  TempMesh fixture("zones");
  write_ascii_mesh(fixture);
  fixture.write("cellZones", ascii_cell_zones());

  const PolyMesh mesh = read_poly_mesh(fixture.path());

  REQUIRE(mesh.cell_zones.size() == 2);
  REQUIRE(mesh.cell_zones[0].name == "fuel");
  REQUIRE(mesh.cell_zones[0].cells == std::vector<int32_t>{0});
  REQUIRE(mesh.cell_zones[1].name == "clad");
  REQUIRE(mesh.cell_zones[1].cells == std::vector<int32_t>{1});
}

TEST_CASE("Read a uniform cellZone list")
{
  TempMesh fixture("uniform");
  write_ascii_mesh(fixture);

  std::string zones = foam_header("regIOobject", "cellZones");
  zones +=
    "1\n(everything\n{\n    type            cellZone;\n"
    "    cellLabels      List<label> 2{1};\n}\n)\n";
  fixture.write("cellZones", zones);

  const PolyMesh mesh = read_poly_mesh(fixture.path());

  REQUIRE(mesh.cell_zones.size() == 1);
  REQUIRE(mesh.cell_zones[0].cells == std::vector<int32_t>{1, 1});
}

TEST_CASE("Read a mesh whose lists all omit their size")
{
  // every top-level list written as ( ... ) with no count in front, which
  // OpenFOAM accepts in ASCII; each face still declares its own vertex count
  TempMesh fixture("unsized");
  fixture.write("points", unsized(ascii_points()));
  fixture.write("faces", unsized(ascii_faces()));
  fixture.write("owner", unsized(ascii_owner()));
  fixture.write("neighbour", unsized(ascii_neighbour()));
  fixture.write("boundary", unsized(ascii_boundary()));

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read unsized cellZones, as they commonly arrive")
{
  // the shape real meshes use: neither the zone list nor each zone's
  // cellLabels carries a count, and a zone may be empty
  TempMesh fixture("unsized_zones");
  write_ascii_mesh(fixture);
  fixture.write("cellZones", foam_header("regIOobject", "cellZones") +
    "(\n"
    "    fuel\n    {\n        type            cellZone;\n"
    "        cellLabels\n        (\n            0\n        )\n        ;\n    }\n"
    "    clad\n    {\n        type            cellZone;\n"
    "        cellLabels\n        (\n            1\n        )\n        ;\n    }\n"
    "    spare\n    {\n        type            cellZone;\n"
    "        cellLabels\n        (\n        )\n        ;\n    }\n"
    ")\n");

  const PolyMesh mesh = read_poly_mesh(fixture.path());

  REQUIRE(mesh.cell_zones.size() == 3);
  REQUIRE(mesh.cell_zones[0].name == "fuel");
  REQUIRE(mesh.cell_zones[0].cells == std::vector<int32_t>{0});
  REQUIRE(mesh.cell_zones[1].name == "clad");
  REQUIRE(mesh.cell_zones[1].cells == std::vector<int32_t>{1});
  REQUIRE(mesh.cell_zones[2].name == "spare");
  REQUIRE(mesh.cell_zones[2].cells.empty());
}

TEST_CASE("Read a binary faceList, as cfMesh writes it")
{
  // most writers use faceCompactList in binary, but cfMesh keeps faceList,
  // each face then a binary labelList of its own
  TempMesh fixture("binary_facelist");
  write_binary_mesh(fixture, 32);
  std::string faces = foam_header("faceList", "faces", "binary");
  faces += std::to_string(N_FACES) + "\n(";
  for (const auto& face : FACES) {
    faces += "\n\n4\n(";
    for (int j = 0; j < 4; j++) put_label(faces, face[j], 32);
    faces += ")";
  }
  fixture.write("faces", faces + "\n)\n");

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read binary points stored in single precision")
{
  // a mixed-precision OpenFOAM build writes scalar=32; the canonical points
  // are small integers, so they survive a float exactly
  TempMesh fixture("float_points");
  write_binary_mesh(fixture, 32);
  std::string points = foam_header("vectorField", "points", "binary");
  points.replace(points.find("scalar=64"), 9, "scalar=32");
  points += std::to_string(N_POINTS) + "\n(";
  for (const auto& p : POINTS) {
    for (int j = 0; j < 3; j++) {
      const float value = static_cast<float>(p[j]);
      char bytes[4];
      std::memcpy(bytes, &value, 4);
      points.append(bytes, 4);
    }
  }
  fixture.write("points", points + ")\n");

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read a neighbour list padded with -1, as cfMesh writes it")
{
  // OpenFOAM accepts neighbour padded with -1 to the length of owner, cutting
  // it at the first -1; cfMesh writes it that way
  TempMesh fixture("padded");
  write_ascii_mesh(fixture);
  std::string neighbour = foam_header("labelList", "neighbour") +
                          std::to_string(N_FACES) + "\n(\n" +
                          std::to_string(NEIGHBOUR[0]) + "\n";
  for (int f = N_INTERNAL_FACES; f < N_FACES; f++) neighbour += "-1\n";
  fixture.write("neighbour", neighbour + ")\n");

  const PolyMesh mesh = read_poly_mesh(fixture.path());
  check_canonical(mesh);
}

TEST_CASE("Read an empty cellZone in either format")
{
  TempMesh fixture("empty_zone");

  SECTION("binary, where an empty list is a bare 0 with no brackets")
  {
    write_binary_mesh(fixture, 32);
    fixture.write("cellZones", binary_cell_zones_with_empty());
  }

  SECTION("ASCII, where an empty list is 0()")
  {
    write_ascii_mesh(fixture);
    fixture.write("cellZones", foam_header("regIOobject", "cellZones") +
      "2\n(fuel\n{\n    type            cellZone;\n"
      "    cellLabels      List<label> 1(0);\n}\n"
      "empty\n{\n    type            cellZone;\n"
      "    cellLabels      List<label> 0();\n}\n)\n");
  }

  const PolyMesh mesh = read_poly_mesh(fixture.path());

  REQUIRE(mesh.cell_zones.size() == 2);
  REQUIRE(mesh.cell_zones[0].cells == std::vector<int32_t>{0});
  REQUIRE(mesh.cell_zones[1].name == "empty");
  REQUIRE(mesh.cell_zones[1].cells.empty());
}

TEST_CASE("Boundary faces are distinguished from internal faces")
{
  TempMesh fixture("boundary");
  write_ascii_mesh(fixture);

  const PolyMesh mesh = read_poly_mesh(fixture.path());

  REQUIRE_FALSE(mesh.is_boundary_face(0));
  for (int f = N_INTERNAL_FACES; f < N_FACES; f++) {
    REQUIRE(mesh.is_boundary_face(f));
  }

  // every boundary face belongs to exactly one patch
  int covered = 0;
  for (const auto& patch : mesh.patches) covered += patch.n_faces;
  REQUIRE(covered == N_FACES - N_INTERNAL_FACES);
}
