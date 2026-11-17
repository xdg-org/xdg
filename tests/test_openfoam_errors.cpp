// stl includes
#include <memory>
#include <stdexcept>
#include <string>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// xdg includes
#include "xdg/config.h"
#include "xdg/constants.h"
#include "xdg/openfoam/mesh_manager.h"

#include "openfoam_fixtures.h"

using namespace xdg;
using namespace xdg::test::openfoam;
using Catch::Matchers::ContainsSubstring;

namespace {

//! Set XDG's fatal error policy for one test, restoring the default after it
//!
//! Under the default policy a fatal error ends the process, which is why these
//! paths could not be tested until XDG offered ErrorPolicy::THROW.
class ErrorPolicyGuard {
public:
  explicit ErrorPolicyGuard(ErrorPolicy policy)
    : previous_(XDGConfig::config().fatal_error_handling())
  {
    XDGConfig::config().set_fatal_error_handling(policy);
  }

  ~ErrorPolicyGuard()
  {
    XDGConfig::config().set_fatal_error_handling(previous_);
  }

  ErrorPolicyGuard(const ErrorPolicyGuard&) = delete;
  ErrorPolicyGuard& operator=(const ErrorPolicyGuard&) = delete;

private:
  ErrorPolicy previous_;
};

//! \p text with its first occurrence of \p from replaced by \p to
std::string replaced(std::string text, const std::string& from, const std::string& to)
{
  return text.replace(text.find(from), from.size(), to);
}

//! Load and initialize a mesh manager over \p path, as a caller would
void load(const std::string& path)
{
  auto mesh_manager = std::make_shared<OpenFOAMMeshManager>();
  mesh_manager->load_file(path);
  mesh_manager->init();
  mesh_manager->parse_metadata();
}

//! The canonical mesh with its owner list cut off partway through
void write_truncated_mesh(const TempMesh& fixture)
{
  write_ascii_mesh(fixture);
  fixture.write("owner", foam_header("labelList", "owner") + "11\n(\n0\n0\n1\n");
}

} // namespace

// ---------------------------------------------------------------------------
// Finding and reading the files
// ---------------------------------------------------------------------------

TEST_CASE("A directory holding no polyMesh is reported")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_empty");

  REQUIRE_THROWS_WITH(load(fixture.path()), ContainsSubstring("Could not find a polyMesh in"));
}

TEST_CASE("A multi-region case is refused, naming its regions")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_regions");
  TempMesh region("err_region");
  StructuredMesh(1, 1, 1).write(region);
  for (const char* name : {"solid", "fluid"}) {
    const fs::path target = fs::path(fixture.path()) / "constant" / name / "polyMesh";
    fs::create_directories(target);
    fs::copy(region.path(), target, fs::copy_options::recursive);
  }

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("multi-region case, with regions: fluid, solid"));
}

TEST_CASE("A compressed mesh file is refused with a way to decompress it")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_gz");
  write_ascii_mesh(fixture);
  fs::rename(fs::path(fixture.path()) / "faces", fs::path(fixture.path()) / "faces.gz");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("faces.gz is compressed") &&
                      ContainsSubstring("foamFormatConvert -constant"));
}

TEST_CASE("A missing mesh file is named")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_missing");
  write_ascii_mesh(fixture);
  fs::remove(fs::path(fixture.path()) / "owner");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Missing polyMesh file") && ContainsSubstring("owner"));
}

TEST_CASE("A truncated list is reported against its file")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_truncated");
  write_truncated_mesh(fixture);

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("owner:") && ContainsSubstring("Unexpected end of file"));
}

TEST_CASE("Big-endian files are refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_msb");
  write_ascii_mesh(fixture);
  fixture.write("points", replaced(ascii_points(), "LSB;", "MSB;"));

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Big-endian OpenFOAM files are not supported"));
}

TEST_CASE("Labels of an unsupported width are refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_width");
  write_ascii_mesh(fixture);
  fixture.write("points", replaced(ascii_points(), "label=32", "label=16"));

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Unsupported label width of 16 bits"));
}

TEST_CASE("Counts that disagree with the header's note are refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_note");
  write_ascii_mesh(fixture);
  fixture.write("owner", replaced(ascii_owner(), "nPoints:12", "nPoints:13"));

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("header declares 13 points but 12 were read"));
}

TEST_CASE("An inverted cell is refused")
{
  // the single tetrahedron with every face wound into the cell rather than out
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_inverted");
  write_single_tet(fixture);
  fixture.write("faces", foam_header("faceList", "faces") +
                         "4\n(\n3(0 1 2)\n3(0 3 1)\n3(0 2 3)\n3(1 3 2)\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Cell 0 is an inverted or degenerate tetrahedron"));
}

TEST_CASE("A list longer than its file is reported rather than allocated")
{
  // the declared length cannot fit in what remains of the file, so the reader
  // must run out of input and say so, not try to reserve a hundred billion labels
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_huge");
  write_ascii_mesh(fixture);
  fixture.write("owner", foam_header("labelList", "owner") + "99999999999\n(\n0\n0\n)\n");

  REQUIRE_THROWS_AS(load(fixture.path()), std::runtime_error);
  REQUIRE_THROWS_WITH(load(fixture.path()), ContainsSubstring("owner"));
}

TEST_CASE("Binary face offsets that do not start at zero are refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_offsets");
  write_binary_mesh(fixture, 32);
  std::string faces = foam_header("faceCompactList", "faces", "binary");
  faces += std::to_string(N_FACES + 1) + "\n(";
  put_label(faces, 1, 32);
  for (int f = 1; f <= N_FACES; f++) put_label(faces, 4 * f, 32);
  faces += ")\n" + std::to_string(N_FACES * 4) + "\n(";
  for (const auto& face : FACES) {
    for (int j = 0; j < 4; j++) put_label(faces, face[j], 32);
  }
  fixture.write("faces", faces + ")\n");

  REQUIRE_THROWS_WITH(load(fixture.path()), ContainsSubstring("face offsets start at 1 rather than 0"));
}

TEST_CASE("A face naming a point that does not exist is refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_point_range");
  write_single_tet(fixture);
  fixture.write("faces", foam_header("faceList", "faces") +
                         "4\n(\n3(0 2 1)\n3(0 1 3)\n3(0 3 2)\n3(1 2 9)\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("face references point 9, outside the 4 points read"));
}

// ---------------------------------------------------------------------------
// Meshes that read cleanly but cannot be used
// ---------------------------------------------------------------------------

TEST_CASE("Overlapping cellZones are refused, naming both")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_overlap");
  StructuredMesh(2, 1, 1).write(fixture);
  fixture.write("cellZones",
    foam_header("regIOobject", "cellZones") +
    "2\n(fuel\n{\n    type            cellZone;\n    cellLabels      List<label> 2(0 1);\n}\n"
    "clad\n{\n    type            cellZone;\n    cellLabels      List<label> 1(1);\n}\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Cell 1 belongs to both cellZone 'fuel' and cellZone 'clad'"));
}

TEST_CASE("A boundary face outside every patch is reported by number")
{
  // with no patches at all there is no range for the reader to check, so this
  // reaches the topology, whose message once printed a literal {}: fatal_error
  // used to take a lone integer argument as its exit status
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_no_patch");
  write_single_tet(fixture);
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") + "0\n(\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()), ContainsSubstring("Boundary face 0 belongs to no patch"));
}

TEST_CASE("Baffles are refused, naming the coincident faces")
{
  // the canonical two-cell mesh with its one internal face split into two
  // coincident boundary faces, one owned by each cell, as createBaffles does
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_baffle");
  write_ascii_mesh(fixture);

  auto face = [](const int* v, bool reversed) {
    return reversed ? "4(" + std::to_string(v[3]) + " " + std::to_string(v[2]) + " " +
                        std::to_string(v[1]) + " " + std::to_string(v[0]) + ")\n"
                    : "4(" + std::to_string(v[0]) + " " + std::to_string(v[1]) + " " +
                        std::to_string(v[2]) + " " + std::to_string(v[3]) + ")\n";
  };
  std::string faces = foam_header("faceList", "faces") + "12\n(\n";
  std::string owner = foam_header("labelList", "owner") + "12\n(\n";
  for (int f = 1; f < N_FACES; f++) {
    faces += face(FACES[f], false);
    owner += std::to_string(OWNER[f]) + "\n";
  }
  faces += face(FACES[0], false) + face(FACES[0], true) + ")\n";
  owner += "0\n1\n)\n";
  fixture.write("faces", faces);
  fixture.write("owner", owner);
  fixture.write("neighbour", foam_header("labelList", "neighbour") + "0()\n");
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") +
    "4\n(\n"
    "    inlet  { type patch; nFaces 1; startFace 0; }\n"
    "    outlet { type patch; nFaces 1; startFace 1; }\n"
    "    walls  { type wall;  nFaces 8; startFace 2; }\n"
    "    baffle { type wall;  nFaces 2; startFace 10; }\n"
    ")\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Boundary faces 10 (patch 'baffle') and 11") &&
                      ContainsSubstring("mergeOrSplitBaffles"));
}

TEST_CASE("Parts that touch without being stitched are refused")
{
  // two unit cubes side by side, each with its own copy of the points on the
  // face where they meet, as mergeMeshes leaves them without stitchMesh, or
  // mergeOrSplitBaffles -split leaves a baffle
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_unstitched");

  std::string points = foam_header("vectorField", "points") + "16\n(\n";
  for (const double shift : {0.0, 1.0}) {
    for (const char* corner : {"0 0 0", "1 0 0", "1 1 0", "0 1 0",
                               "0 0 1", "1 0 1", "1 1 1", "0 1 1"}) {
      std::string c = corner;
      c[0] = static_cast<char>('0' + (c[0] - '0') + static_cast<int>(shift));
      points += "(" + c + ")\n";
    }
  }
  fixture.write("points", points + ")\n");

  // each cube's faces wound outward; the left cube's +x face and the right
  // cube's -x face are the two that coincide
  auto quad = [](int a, int b, int c, int d) {
    return "4(" + std::to_string(a) + " " + std::to_string(b) + " " +
           std::to_string(c) + " " + std::to_string(d) + ")\n";
  };
  std::string faces = foam_header("faceList", "faces") + "12\n(\n";
  faces += quad(0, 3, 2, 1) + quad(4, 5, 6, 7) + quad(0, 1, 5, 4) + quad(3, 7, 6, 2) +
           quad(0, 4, 7, 3) + quad(1, 2, 6, 5);
  faces += quad(8, 12, 15, 11);
  faces += quad(8, 11, 10, 9) + quad(12, 13, 14, 15) + quad(8, 9, 13, 12) +
           quad(11, 15, 14, 10) + quad(9, 10, 14, 13);
  fixture.write("faces", faces + ")\n");
  fixture.write("owner", foam_header("labelList", "owner") +
                         "12\n(\n0\n0\n0\n0\n0\n0\n1\n1\n1\n1\n1\n1\n)\n");
  fixture.write("neighbour", foam_header("labelList", "neighbour") + "0()\n");
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") +
    "4\n(\n"
    "    leftOuter  { type wall; nFaces 5; startFace 0; }\n"
    "    leftTouch  { type wall; nFaces 1; startFace 5; }\n"
    "    rightTouch { type wall; nFaces 1; startFace 6; }\n"
    "    rightOuter { type wall; nFaces 5; startFace 7; }\n"
    ")\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Boundary faces 5 (patch 'leftTouch') and 6 "
                                        "(patch 'rightTouch') occupy the same place") &&
                      ContainsSubstring("stitchMesh"));
}

TEST_CASE("A cyclic patch is refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_cyclic");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"left", "cyclic"}, {"right", "cyclic"}, {"ymin", "patch"},
     {"ymax", "patch"}, {"zmin", "patch"}, {"zmax", "patch"}});

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Patch 'left' has type 'cyclic', a periodic boundary"));
}

TEST_CASE("A processor patch from a decomposed case is refused")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_processor");
  StructuredMesh(2, 2, 2).write(fixture, /*split_zones=*/false,
    {{"procBoundary0to1", "processor"}, {"xmax", "patch"}, {"ymin", "patch"},
     {"ymax", "patch"}, {"zmin", "patch"}, {"zmax", "patch"}});

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Patch 'procBoundary0to1' has type 'processor'") &&
                      ContainsSubstring("reconstructParMesh"));
}

TEST_CASE("A cell that is neither a tet nor a hex is refused")
{
  // a square pyramid: one quadrilateral base and four triangular sides, each
  // wound to point out of the cell
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_pyramid");
  const std::string note = "nPoints:5  nCells:1  nFaces:5  nInternalFaces:0";
  fixture.write("points", foam_header("vectorField", "points") +
                          "5\n(\n(0 0 0)\n(1 0 0)\n(1 1 0)\n(0 1 0)\n(0.5 0.5 1)\n)\n");
  fixture.write("faces", foam_header("faceList", "faces") +
                         "5\n(\n4(0 3 2 1)\n3(0 1 4)\n3(1 2 4)\n3(2 3 4)\n3(3 0 4)\n)\n");
  fixture.write("owner", foam_header("labelList", "owner", "ascii", note) +
                         "5\n(\n0\n0\n0\n0\n0\n)\n");
  fixture.write("neighbour", foam_header("labelList", "neighbour", "ascii", note) + "0()\n");
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") +
                            "1\n(\n    walls\n    {\n        type            wall;\n"
                            "        nFaces          5;\n        startFace       0;\n    }\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Cell 0 is a") &&
                      ContainsSubstring("XDG supports only tetrahedra and hexahedra"));
}

TEST_CASE("A cellZone mixing tetrahedra and hexahedra is refused, naming both")
{
  ErrorPolicyGuard guard(ErrorPolicy::THROW);
  TempMesh fixture("err_mixed_zone");

  // a unit cube and, apart from it, a unit tetrahedron, each on its own patch
  // so that every surface has a single face type and only the volume is mixed
  fixture.write("points",
    foam_header("vectorField", "points") +
    "12\n(\n(0 0 0)\n(1 0 0)\n(1 1 0)\n(0 1 0)\n(0 0 1)\n(1 0 1)\n(1 1 1)\n(0 1 1)\n"
    "(2 0 0)\n(3 0 0)\n(2 1 0)\n(2 0 1)\n)\n");
  fixture.write("faces",
    foam_header("faceList", "faces") +
    "10\n(\n4(0 3 2 1)\n4(4 5 6 7)\n4(0 1 5 4)\n4(3 7 6 2)\n4(0 4 7 3)\n4(1 2 6 5)\n"
    "3(8 10 9)\n3(8 9 11)\n3(8 11 10)\n3(9 10 11)\n)\n");
  const std::string note = "nPoints:12  nCells:2  nFaces:10  nInternalFaces:0";
  fixture.write("owner", foam_header("labelList", "owner", "ascii", note) +
                         "10\n(\n0\n0\n0\n0\n0\n0\n1\n1\n1\n1\n)\n");
  fixture.write("neighbour", foam_header("labelList", "neighbour", "ascii", note) + "0()\n");
  fixture.write("boundary", foam_header("polyBoundaryMesh", "boundary") +
    "2\n(\n    cube { type wall; nFaces 6; startFace 0; }\n"
    "    tet { type wall; nFaces 4; startFace 6; }\n)\n");
  fixture.write("cellZones", foam_header("regIOobject", "cellZones") +
    "1\n(\nfuel\n{\n    type cellZone;\n    cellLabels List<label> 2(0 1);\n}\n)\n");

  REQUIRE_THROWS_WITH(load(fixture.path()),
                      ContainsSubstring("Volume 1 mixes HEX and TET elements"));
}

// ---------------------------------------------------------------------------
// Error policy
// ---------------------------------------------------------------------------

TEST_CASE("Told to carry on past a fatal error, the backend throws instead")
{
  // nothing can be parsed past a broken file, so a fatal_error that returns
  // must not let the reader keep going
  ErrorPolicyGuard guard(ErrorPolicy::NONE);
  TempMesh fixture("err_policy_none");
  write_truncated_mesh(fixture);

  REQUIRE_THROWS_AS(load(fixture.path()), std::runtime_error);
  REQUIRE_THROWS_WITH(load(fixture.path()), ContainsSubstring("Unexpected end of file"));
}
