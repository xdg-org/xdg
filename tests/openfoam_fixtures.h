#ifndef _XDG_TEST_OPENFOAM_FIXTURES
#define _XDG_TEST_OPENFOAM_FIXTURES

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace xdg::test::openfoam {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// The two-cell hex mesh blockMesh produces for a 2x1x1 block spanning
// (0 0 0) to (2 1 1). Reproduced verbatim from real OpenFOAM output so the
// fixtures exercise the format as it is actually written, including the face
// winding and the upper-triangular owner/neighbour ordering.
//
// The `walls` patch is useful beyond the reader: its eight faces alternate
// between the two cells, so once the cells are placed in different zones it
// becomes a patch that straddles a volume boundary.
// ---------------------------------------------------------------------------

constexpr int N_POINTS = 12;
constexpr int N_FACES = 11;
constexpr int N_INTERNAL_FACES = 1;
constexpr int N_CELLS = 2;

inline const double POINTS[N_POINTS][3] = {
  {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0},
  {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {0, 1, 1}, {1, 1, 1}, {2, 1, 1}
};

inline const int FACES[N_FACES][4] = {
  {1, 4, 10, 7}, {0, 6, 9, 3}, {2, 5, 11, 8}, {0, 1, 7, 6},
  {1, 2, 8, 7},  {3, 9, 10, 4}, {4, 10, 11, 5}, {0, 3, 4, 1},
  {1, 4, 5, 2},  {6, 7, 10, 9}, {7, 8, 11, 10}
};

inline const int OWNER[N_FACES] = {0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
inline const int NEIGHBOUR[N_INTERNAL_FACES] = {1};

inline const char* MESH_NOTE = "nPoints:12  nCells:2  nFaces:11  nInternalFaces:1";

// ---------------------------------------------------------------------------
// Fixture construction
// ---------------------------------------------------------------------------

//! A polyMesh directory that removes itself when the test ends
class TempMesh {
public:
  explicit TempMesh(const std::string& name)
  {
    // the random part keeps two test processes running at once, from different
    // build directories say, from clearing each other's fixtures
    static const std::string process_tag = std::to_string(std::random_device {}());
    static int counter = 0;
    dir_ = fs::temp_directory_path() /
           ("xdg_openfoam_" + process_tag + "_" + name + "_" + std::to_string(counter++));
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }

  ~TempMesh()
  {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  TempMesh(const TempMesh&) = delete;
  TempMesh& operator=(const TempMesh&) = delete;

  //! Write \p contents to \p name, creating parent directories as needed
  void write(const std::string& name, const std::string& contents) const
  {
    const fs::path file = dir_ / name;
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }

  std::string path() const { return dir_.string(); }

private:
  fs::path dir_;
};

inline std::string foam_header(const std::string& class_name, const std::string& object,
                               const std::string& format = "ascii",
                               const std::string& note = "",
                               int label_bits = 32)
{
  std::string text =
    "FoamFile\n{\n"
    "    version     2.0;\n"
    "    format      " + format + ";\n"
    "    arch        \"LSB;label=" + std::to_string(label_bits) + ";scalar=64\";\n";
  if (!note.empty()) text += "    note        \"" + note + "\";\n";
  text += "    class       " + class_name + ";\n"
          "    location    \"constant/polyMesh\";\n"
          "    object      " + object + ";\n}\n"
          "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
  return text;
}

// Little-endian encoders, matching the LSB declared in the fixture headers
inline void put_label(std::string& out, int64_t value, int label_bits)
{
  const int bytes = label_bits / 8;
  for (int i = 0; i < bytes; i++) {
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
  }
}

inline void put_double(std::string& out, double value)
{
  char buffer[8];
  std::memcpy(buffer, &value, 8);
  out.append(buffer, 8);
}

inline std::string ascii_points()
{
  std::string text = foam_header("vectorField", "points");
  text += std::to_string(N_POINTS) + "\n(\n";
  for (const auto& p : POINTS) {
    text += "(" + std::to_string(p[0]) + " " + std::to_string(p[1]) + " " +
            std::to_string(p[2]) + ")\n";
  }
  text += ")\n";
  return text;
}

inline std::string ascii_faces()
{
  std::string text = foam_header("faceList", "faces");
  text += std::to_string(N_FACES) + "\n(\n";
  for (const auto& f : FACES) {
    text += "4(" + std::to_string(f[0]) + " " + std::to_string(f[1]) + " " +
            std::to_string(f[2]) + " " + std::to_string(f[3]) + ")\n";
  }
  text += ")\n";
  return text;
}

//! `owner` is written one entry per line, as OpenFOAM does for longer lists
inline std::string ascii_owner()
{
  std::string text = foam_header("labelList", "owner", "ascii", MESH_NOTE);
  text += std::to_string(N_FACES) + "\n(\n";
  for (const int cell : OWNER) text += std::to_string(cell) + "\n";
  text += ")\n";
  return text;
}

//! `neighbour` is written inline, as OpenFOAM does for short lists. This is the
//! spelling that breaks a line-oriented reader.
inline std::string ascii_neighbour()
{
  std::string text = foam_header("labelList", "neighbour", "ascii", MESH_NOTE);
  text += "1(1)\n";
  return text;
}

inline std::string ascii_boundary()
{
  std::string text = foam_header("polyBoundaryMesh", "boundary");
  text +=
    "3\n(\n"
    "    inlet\n    {\n        type            patch;\n"
    "        nFaces          1;\n        startFace       1;\n    }\n"
    "    outlet\n    {\n        type            patch;\n"
    "        nFaces          1;\n        startFace       2;\n    }\n"
    "    walls\n    {\n        type            wall;\n"
    "        inGroups        1(wall);\n"
    "        nFaces          8;\n        startFace       3;\n    }\n"
    ")\n";
  return text;
}

//! Two zones, one cell each, using the typed `List<label>` spelling
inline std::string ascii_cell_zones()
{
  std::string text =
    "FoamFile\n{\n    version     2.0;\n    format      ascii;\n"
    "    arch        \"LSB;label=32;scalar=64\";\n"
    "    class       regIOobject;\n    location    \"constant/polyMesh\";\n"
    "    object      cellZones;\n"
    "    meta\n    {\n        names           2 ( fuel clad );\n    }\n}\n"
    "// * * * * //\n\n";
  text +=
    "2\n"
    "(fuel\n{\n    type            cellZone;\n"
    "    cellLabels      List<label> 1(0);\n}\n"
    "clad\n{\n    type            cellZone;\n"
    "    cellLabels      List<label> 1(1);\n}\n"
    ")\n";
  return text;
}

//! A single zone naming \p cell, leaving the other cell unzoned
inline std::string ascii_partial_cell_zone(int cell, const std::string& name = "fuel")
{
  std::string text = foam_header("regIOobject", "cellZones");
  text += "1\n(" + name + "\n{\n    type            cellZone;\n"
          "    cellLabels      List<label> 1(" + std::to_string(cell) + ");\n}\n)\n";
  return text;
}

inline void write_ascii_mesh(const TempMesh& mesh, const std::string& prefix = "")
{
  mesh.write(prefix + "points", ascii_points());
  mesh.write(prefix + "faces", ascii_faces());
  mesh.write(prefix + "owner", ascii_owner());
  mesh.write(prefix + "neighbour", ascii_neighbour());
  mesh.write(prefix + "boundary", ascii_boundary());
}

//! cellZones as OpenFOAM writes them in binary when one zone selected nothing
//!
//! The non-empty zone carries a raw payload; the empty one is a bare `0` with
//! no brackets at all, exactly as topoSet leaves it.
inline std::string binary_cell_zones_with_empty(int label_bits = 32)
{
  std::string text = foam_header("regIOobject", "cellZones", "binary", "", label_bits);
  text += "2\n(fuel\n{\n    type            cellZone;\n"
          "    cellLabels      List<label> \n1\n(";
  put_label(text, 0, label_bits);
  text += ");\n}\nempty\n{\n    type            cellZone;\n"
          "    cellLabels      List<label> 0;\n}\n)\n";
  return text;
}

inline void write_binary_mesh(const TempMesh& mesh, int label_bits)
{
  // points: a raw vectorField
  std::string points = foam_header("vectorField", "points", "binary", "", label_bits);
  points += std::to_string(N_POINTS) + "\n(";
  for (const auto& p : POINTS) {
    put_double(points, p[0]);
    put_double(points, p[1]);
    put_double(points, p[2]);
  }
  points += ")\n";
  mesh.write("points", points);

  // faces: a compact list, i.e. an offsets block followed by a values block
  std::string faces = foam_header("faceCompactList", "faces", "binary", "", label_bits);
  faces += std::to_string(N_FACES + 1) + "\n(";
  for (int f = 0; f <= N_FACES; f++) put_label(faces, 4 * f, label_bits);
  faces += ")\n";
  faces += std::to_string(N_FACES * 4) + "\n(";
  for (const auto& f : FACES) {
    for (int j = 0; j < 4; j++) put_label(faces, f[j], label_bits);
  }
  faces += ")\n";
  mesh.write("faces", faces);

  std::string owner = foam_header("labelList", "owner", "binary", MESH_NOTE, label_bits);
  owner += std::to_string(N_FACES) + "\n(";
  for (const int cell : OWNER) put_label(owner, cell, label_bits);
  owner += ")\n";
  mesh.write("owner", owner);

  std::string neighbour =
    foam_header("labelList", "neighbour", "binary", MESH_NOTE, label_bits);
  neighbour += std::to_string(N_INTERNAL_FACES) + "\n(";
  for (const int cell : NEIGHBOUR) put_label(neighbour, cell, label_bits);
  neighbour += ")\n";
  mesh.write("neighbour", neighbour);

  // the boundary dictionary stays ASCII even in a binary mesh
  mesh.write("boundary", ascii_boundary());
}

// ---------------------------------------------------------------------------
// Generated meshes
//
// Written directly rather than through blockMesh so the tests stay free of an
// OpenFOAM installation, but following the same conventions: internal faces
// first, owner below neighbour, and every face wound so its normal runs from
// the owner into the neighbour.
// ---------------------------------------------------------------------------

//! An nx by ny by nz grid of hexahedra spanning the box (0 0 0) to (lx ly lz)
//!
//! Six patches, named for the face of the box they cover. The geometry is
//! regular, so distances through it can be checked against arithmetic.
class StructuredMesh {
public:
  StructuredMesh(int nx, int ny, int nz, double lx = 1.0, double ly = 1.0,
                 double lz = 1.0)
    : nx_(nx), ny_(ny), nz_(nz), lx_(lx), ly_(ly), lz_(lz)
  {}

  int num_cells() const { return nx_ * ny_ * nz_; }
  double dx() const { return lx_ / nx_; }
  double dy() const { return ly_ / ny_; }
  double dz() const { return lz_ / nz_; }

  //! A patch's name and OpenFOAM type
  using Patch = std::pair<std::string, std::string>;

  //! Write the mesh into \p target
  //! \param split_zones Split the cells into two cellZones at x = lx/2
  //! \param patches Names and types for the six box faces, in the order
  //!        xmin, xmax, ymin, ymax, zmin, zmax. Defaults to those names with
  //!        type "patch".
  void write(const TempMesh& target, bool split_zones = false,
             const std::vector<Patch>& patches = {}) const
  {
    build();
    target.write("points", points_text());
    target.write("faces", faces_text());
    target.write("owner", labels_text("owner", owner_));
    target.write("neighbour", labels_text("neighbour", neighbour_));
    target.write("boundary", boundary_text(patches));
    if (split_zones) target.write("cellZones", zones_text());
  }

private:
  int point_id(int i, int j, int k) const
  {
    return i + j * (nx_ + 1) + k * (nx_ + 1) * (ny_ + 1);
  }

  int cell_id(int i, int j, int k) const { return i + j * nx_ + k * nx_ * ny_; }

  //! Add a face, reversing the winding when the normal must point the other way
  void add_face(std::vector<int> face, int owner, int neighbour, bool reverse) const
  {
    if (reverse) std::reverse(face.begin(), face.end());
    faces_.push_back(std::move(face));
    owner_.push_back(owner);
    if (neighbour >= 0) neighbour_.push_back(neighbour);
  }

  // faces normal to each axis, wound so the normal points along the positive
  // axis when taken in the stored order
  std::vector<int> x_face(int i, int j, int k) const
  {
    return {point_id(i, j, k), point_id(i, j + 1, k), point_id(i, j + 1, k + 1),
            point_id(i, j, k + 1)};
  }
  std::vector<int> y_face(int i, int j, int k) const
  {
    return {point_id(i, j, k), point_id(i, j, k + 1), point_id(i + 1, j, k + 1),
            point_id(i + 1, j, k)};
  }
  std::vector<int> z_face(int i, int j, int k) const
  {
    return {point_id(i, j, k), point_id(i + 1, j, k), point_id(i + 1, j + 1, k),
            point_id(i, j + 1, k)};
  }

  void build() const
  {
    faces_.clear();
    owner_.clear();
    neighbour_.clear();
    patch_sizes_.assign(6, 0);

    // internal faces come first, and each is owned by the lower-numbered cell
    for (int k = 0; k < nz_; k++)
      for (int j = 0; j < ny_; j++)
        for (int i = 1; i < nx_; i++)
          add_face(x_face(i, j, k), cell_id(i - 1, j, k), cell_id(i, j, k), false);

    for (int k = 0; k < nz_; k++)
      for (int j = 1; j < ny_; j++)
        for (int i = 0; i < nx_; i++)
          add_face(y_face(i, j, k), cell_id(i, j - 1, k), cell_id(i, j, k), false);

    for (int k = 1; k < nz_; k++)
      for (int j = 0; j < ny_; j++)
        for (int i = 0; i < nx_; i++)
          add_face(z_face(i, j, k), cell_id(i, j, k - 1), cell_id(i, j, k), false);

    n_internal_ = static_cast<int>(faces_.size());

    // then the boundary patches, each wound to point out of the box
    for (int k = 0; k < nz_; k++)
      for (int j = 0; j < ny_; j++)
        add_face(x_face(0, j, k), cell_id(0, j, k), -1, true);
    patch_sizes_[0] = ny_ * nz_;

    for (int k = 0; k < nz_; k++)
      for (int j = 0; j < ny_; j++)
        add_face(x_face(nx_, j, k), cell_id(nx_ - 1, j, k), -1, false);
    patch_sizes_[1] = ny_ * nz_;

    for (int k = 0; k < nz_; k++)
      for (int i = 0; i < nx_; i++)
        add_face(y_face(i, 0, k), cell_id(i, 0, k), -1, true);
    patch_sizes_[2] = nx_ * nz_;

    for (int k = 0; k < nz_; k++)
      for (int i = 0; i < nx_; i++)
        add_face(y_face(i, ny_, k), cell_id(i, ny_ - 1, k), -1, false);
    patch_sizes_[3] = nx_ * nz_;

    for (int j = 0; j < ny_; j++)
      for (int i = 0; i < nx_; i++)
        add_face(z_face(i, j, 0), cell_id(i, j, 0), -1, true);
    patch_sizes_[4] = nx_ * ny_;

    for (int j = 0; j < ny_; j++)
      for (int i = 0; i < nx_; i++)
        add_face(z_face(i, j, nz_), cell_id(i, j, nz_ - 1), -1, false);
    patch_sizes_[5] = nx_ * ny_;
  }

  std::string note() const
  {
    return "nPoints:" + std::to_string((nx_ + 1) * (ny_ + 1) * (nz_ + 1)) +
           "  nCells:" + std::to_string(num_cells()) +
           "  nFaces:" + std::to_string(faces_.size()) +
           "  nInternalFaces:" + std::to_string(n_internal_);
  }

  std::string points_text() const
  {
    std::string text = foam_header("vectorField", "points");
    text += std::to_string((nx_ + 1) * (ny_ + 1) * (nz_ + 1)) + "\n(\n";
    for (int k = 0; k <= nz_; k++)
      for (int j = 0; j <= ny_; j++)
        for (int i = 0; i <= nx_; i++)
          text += "(" + std::to_string(i * dx()) + " " + std::to_string(j * dy()) +
                  " " + std::to_string(k * dz()) + ")\n";
    text += ")\n";
    return text;
  }

  std::string faces_text() const
  {
    std::string text = foam_header("faceList", "faces");
    text += std::to_string(faces_.size()) + "\n(\n";
    for (const auto& face : faces_) {
      text += std::to_string(face.size()) + "(";
      for (size_t i = 0; i < face.size(); i++) {
        if (i > 0) text += " ";
        text += std::to_string(face[i]);
      }
      text += ")\n";
    }
    text += ")\n";
    return text;
  }

  std::string labels_text(const std::string& object,
                          const std::vector<int>& values) const
  {
    std::string text = foam_header("labelList", object, "ascii", note());
    text += std::to_string(values.size()) + "\n(\n";
    for (const int value : values) text += std::to_string(value) + "\n";
    text += ")\n";
    return text;
  }

  std::string boundary_text(const std::vector<Patch>& overrides) const
  {
    static const char* names[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
    std::string text = foam_header("polyBoundaryMesh", "boundary");
    text += "6\n(\n";
    int start = n_internal_;
    for (int p = 0; p < 6; p++) {
      const std::string name =
        p < static_cast<int>(overrides.size()) ? overrides[p].first : names[p];
      const std::string type =
        p < static_cast<int>(overrides.size()) ? overrides[p].second : "patch";
      text += "    " + name + "\n    {\n        type            " + type + ";\n" +
              "        nFaces          " + std::to_string(patch_sizes_[p]) + ";\n" +
              "        startFace       " + std::to_string(start) + ";\n    }\n";
      start += patch_sizes_[p];
    }
    text += ")\n";
    return text;
  }

  //! Two zones split at the midplane in x, so the interface is a full plane
  std::string zones_text() const
  {
    std::vector<int> left;
    std::vector<int> right;
    for (int k = 0; k < nz_; k++)
      for (int j = 0; j < ny_; j++)
        for (int i = 0; i < nx_; i++)
          (i < nx_ / 2 ? left : right).push_back(cell_id(i, j, k));

    auto zone = [](const std::string& name, const std::vector<int>& cells) {
      std::string text = name + "\n{\n    type            cellZone;\n" +
                         "    cellLabels      List<label> " +
                         std::to_string(cells.size()) + "(";
      for (size_t i = 0; i < cells.size(); i++) {
        if (i > 0) text += " ";
        text += std::to_string(cells[i]);
      }
      return text + ");\n}\n";
    };

    std::string text = foam_header("regIOobject", "cellZones");
    text += "2\n(" + zone("fuel", left) + zone("clad", right) + ")\n";
    return text;
  }

  int nx_, ny_, nz_;
  double lx_, ly_, lz_;

  mutable std::vector<std::vector<int>> faces_;
  mutable std::vector<int> owner_;
  mutable std::vector<int> neighbour_;
  mutable std::vector<int> patch_sizes_;
  mutable int n_internal_ {0};
};

//! A single tetrahedron with vertices at the origin and the three unit axes
//!
//! Exercises the triangular-face and tetrahedral-element paths, which the
//! hexahedral fixtures never reach. Its volume is 1/6.
inline void write_single_tet(const TempMesh& target)
{
  target.write("points",
    foam_header("vectorField", "points") +
    "4\n(\n(0 0 0)\n(1 0 0)\n(0 1 0)\n(0 0 1)\n)\n");

  // all four faces are on the boundary, wound to point out of the cell
  target.write("faces",
    foam_header("faceList", "faces") +
    "4\n(\n3(0 2 1)\n3(0 1 3)\n3(0 3 2)\n3(1 2 3)\n)\n");

  const std::string note = "nPoints:4  nCells:1  nFaces:4  nInternalFaces:0";
  target.write("owner",
    foam_header("labelList", "owner", "ascii", note) + "4\n(\n0\n0\n0\n0\n)\n");
  target.write("neighbour",
    foam_header("labelList", "neighbour", "ascii", note) + "0()\n");

  target.write("boundary",
    foam_header("polyBoundaryMesh", "boundary") +
    "1\n(\n    walls\n    {\n        type            wall;\n"
    "        nFaces          4;\n        startFace       0;\n    }\n)\n");
}

} // namespace xdg::test::openfoam

#endif // include guard
