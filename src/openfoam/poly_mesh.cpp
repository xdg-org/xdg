#include "xdg/openfoam/poly_mesh.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>

#include "xdg/openfoam/foam_error.h"
#include "xdg/openfoam/foam_file.h"
#include "xdg/openfoam/foam_token_stream.h"

namespace xdg {

namespace fs = std::filesystem;

namespace {

//! Narrow a parsed label to the 32-bit index space XDG uses throughout
int32_t narrow(int64_t value, const FoamTokenStream& ts)
{
  if (value < std::numeric_limits<int32_t>::min() ||
      value > std::numeric_limits<int32_t>::max()) {
    ts.error(fmt::format("Label {} does not fit in the 32-bit index space", value));
  }
  return static_cast<int32_t>(value);
}

//! A list that gave no length before its opening bracket
constexpr int64_t UNSIZED_LIST = -1;

//! Read a list length, accepting the typed spelling `List<label> 12`
//!
//! OpenFOAM also permits an ASCII list to omit its length and simply run
//! `( a b c )`, which is how cellZones are commonly written. That is reported
//! as UNSIZED_LIST, leaving the caller to read up to the closing bracket.
int64_t read_list_length(FoamTokenStream& ts)
{
  if (ts.peek() == "List") {
    ts.next();
    ts.expect("<");
    ts.next(); // element type
    ts.expect(">");
  }
  if (ts.at_char('(')) return UNSIZED_LIST;

  const int64_t length = ts.next_label();
  if (length < 0) ts.error(fmt::format("Negative list length {}", length));
  return length;
}

//! Capacity to reserve for a list declared to hold \p n entries
//!
//! Every entry takes at least one byte of the file, so a declared length
//! beyond what remains is a malformed file. Reserving it outright would fail
//! with std::bad_alloc before the reader could say what is wrong; capped, the
//! read runs out of input and reports that instead.
size_t capacity_for(int64_t n, const FoamTokenStream& ts)
{
  return n > 0 ? std::min(static_cast<size_t>(n), ts.remaining()) : 0;
}

//! Decode \p n labels of the declared width from a raw payload
void decode_labels(std::vector<int32_t>& out, const char* data, int64_t n,
                   const FoamHeader& header, const FoamTokenStream& ts)
{
  out.reserve(out.size() + static_cast<size_t>(n));
  if (header.arch.label_bits == 32) {
    for (int64_t i = 0; i < n; i++) {
      int32_t value;
      std::memcpy(&value, data + i * 4, 4);
      out.push_back(value);
    }
  } else {
    for (int64_t i = 0; i < n; i++) {
      int64_t value;
      std::memcpy(&value, data + i * 8, 8);
      out.push_back(narrow(value, ts));
    }
  }
}

//! Read a list of labels in either storage format
std::vector<int32_t> read_label_list(FoamTokenStream& ts, const FoamHeader& header)
{
  const int64_t n = read_list_length(ts);
  std::vector<int32_t> values;
  values.reserve(capacity_for(n, ts));

  // an empty list may carry no brackets at all: OpenFOAM writes an empty
  // binary list as plain `0`, as in `cellLabels List<label> 0;`
  if (n == 0 && !ts.at_char('(') && !ts.at_char('{')) return values;

  if (header.format == FoamFormat::BINARY) {
    if (n == UNSIZED_LIST) {
      ts.error("A binary list must declare its length");
    }
    ts.open_bracket();
    const char* data = ts.raw(static_cast<size_t>(n) * header.label_bytes());
    decode_labels(values, data, n, header, ts);
    ts.close_bracket();
    return values;
  }

  if (n == UNSIZED_LIST) {
    ts.open_bracket();
    while (!ts.at_char(')')) values.push_back(narrow(ts.next_label(), ts));
    ts.close_bracket();
    return values;
  }

  // uniform list: `10{0}` means ten copies of zero
  if (ts.peek() == "{") {
    ts.expect("{");
    const int32_t value = narrow(ts.next_label(), ts);
    ts.expect("}");
    values.assign(static_cast<size_t>(n), value);
    return values;
  }

  values.reserve(capacity_for(n, ts));
  ts.open_bracket();
  for (int64_t i = 0; i < n; i++) {
    values.push_back(narrow(ts.next_label(), ts));
  }
  ts.close_bracket();
  return values;
}

//! Read a field of three-component vectors in either storage format
std::vector<Vertex> read_point_field(FoamTokenStream& ts, const FoamHeader& header)
{
  const int64_t n = read_list_length(ts);
  std::vector<Vertex> points;
  points.reserve(capacity_for(n, ts));

  if (n == UNSIZED_LIST && header.format == FoamFormat::BINARY) {
    ts.error("A binary list must declare its length");
  }

  if (header.format == FoamFormat::BINARY) {
    const int width = header.scalar_bytes();
    ts.open_bracket();
    const char* data = ts.raw(static_cast<size_t>(n) * 3 * width);
    for (int64_t i = 0; i < n; i++) {
      double c[3];
      for (int j = 0; j < 3; j++) {
        const char* element = data + (i * 3 + j) * width;
        if (width == 8) {
          double value;
          std::memcpy(&value, element, 8);
          c[j] = value;
        } else {
          float value;
          std::memcpy(&value, element, 4);
          c[j] = static_cast<double>(value);
        }
      }
      points.emplace_back(c[0], c[1], c[2]);
    }
    ts.close_bracket();
    return points;
  }

  ts.open_bracket();
  for (int64_t i = 0; n == UNSIZED_LIST ? !ts.at_char(')') : i < n; i++) {
    ts.expect("(");
    const double x = ts.next_scalar();
    const double y = ts.next_scalar();
    const double z = ts.next_scalar();
    ts.expect(")");
    points.emplace_back(x, y, z);
  }
  ts.close_bracket();
  return points;
}

//! Read the face list, which OpenFOAM stores as a plain list in ASCII and as a
//! compressed-row pair of offset and value lists in binary
void read_face_list(FoamTokenStream& ts, const FoamHeader& header,
                    std::vector<int32_t>& offsets, std::vector<int32_t>& values)
{
  if (header.class_name == "faceCompactList") {
    offsets = read_label_list(ts, header);
    values = read_label_list(ts, header);
    if (offsets.empty()) {
      ts.error("A faceCompactList must carry at least one offset");
    }
    return;
  }

  if (header.class_name != "faceList") {
    ts.error(fmt::format("Cannot read faces of class '{}'; expected faceList or "
                         "faceCompactList", header.class_name));
  }

  // a plain faceList is a list of labelLists. Most writers switch to the
  // compact form in binary, but some (cfMesh among them) keep this one, each
  // face then being a binary labelList of its own.
  const int64_t n = read_list_length(ts);
  offsets.clear();
  offsets.reserve(capacity_for(n, ts) + 1);
  offsets.push_back(0);
  values.clear();

  ts.open_bracket();
  for (int64_t f = 0; n == UNSIZED_LIST ? !ts.at_char(')') : f < n; f++) {
    if (header.format == FoamFormat::BINARY) {
      const std::vector<int32_t> face = read_label_list(ts, header);
      values.insert(values.end(), face.begin(), face.end());
    } else {
      const int64_t n_vertices = read_list_length(ts);
      if (n_vertices == UNSIZED_LIST) ts.error("A face must declare its size");
      ts.open_bracket();
      for (int64_t j = 0; j < n_vertices; j++) {
        values.push_back(narrow(ts.next_label(), ts));
      }
      ts.close_bracket();
    }
    offsets.push_back(static_cast<int32_t>(values.size()));
  }
  ts.close_bracket();
}

//! Read the boundary patch dictionary list
std::vector<PolyMeshPatch> read_boundary(FoamTokenStream& ts)
{
  const int64_t n = read_list_length(ts);
  std::vector<PolyMeshPatch> patches;
  patches.reserve(capacity_for(n, ts));

  ts.open_bracket();
  for (int64_t i = 0; n == UNSIZED_LIST ? !ts.at_char(')') : i < n; i++) {
    PolyMeshPatch patch;
    patch.name = ts.next();
    ts.expect("{");

    while (true) {
      const std::string key = ts.next();
      if (key == "}") break;
      if (ts.peek() == "{") {
        ts.skip_dict_block();
        continue;
      }

      std::vector<std::string> values;
      while (true) {
        const std::string token = ts.next();
        if (token == ";") break;
        values.push_back(token);
      }
      if (values.empty()) continue;

      if (key == "inGroups") {
        // written as `1(wall)` or `List<word> 2(wall shield)`: the group names
        // are the words inside the brackets
        const auto open = std::find(values.begin(), values.end(), "(");
        const auto close = std::find(values.begin(), values.end(), ")");
        if (open != values.end() && close != values.end() && open < close) {
          patch.groups.assign(open + 1, close);
        }
      }
      else if (key == "type") patch.type = values[0];
      else if (key == "nFaces") patch.n_faces = narrow(std::atoll(values[0].c_str()), ts);
      else if (key == "startFace") patch.start_face = narrow(std::atoll(values[0].c_str()), ts);
    }
    patches.push_back(std::move(patch));
  }
  ts.close_bracket();
  return patches;
}

//! Read the cell zone dictionary list
std::vector<PolyMeshCellZone> read_cell_zones(FoamTokenStream& ts, const FoamHeader& header)
{
  const int64_t n = read_list_length(ts);
  std::vector<PolyMeshCellZone> zones;
  zones.reserve(capacity_for(n, ts));

  ts.open_bracket();
  for (int64_t i = 0; n == UNSIZED_LIST ? !ts.at_char(')') : i < n; i++) {
    PolyMeshCellZone zone;
    zone.name = ts.next();
    ts.expect("{");

    while (true) {
      const std::string key = ts.next();
      if (key == "}") break;
      if (key == "cellLabels") {
        zone.cells = read_label_list(ts, header);
        ts.expect(";");
        continue;
      }
      if (ts.peek() == "{") {
        ts.skip_dict_block();
        continue;
      }
      ts.skip_to_semicolon();
    }
    zones.push_back(std::move(zone));
  }
  ts.close_bracket();
  return zones;
}

//! Locate the polyMesh directory for \p path, which may name either a case
//! directory or the polyMesh directory itself
fs::path resolve_poly_mesh_dir(const std::string& path)
{
  const fs::path root {path};

  if (!fs::exists(root)) {
    foam_fatal_error("No such OpenFOAM path: {}", path);
  }

  if (fs::exists(root / "points") || fs::exists(root / "points.gz")) {
    return root;
  }

  const fs::path constant = root / "constant";
  const fs::path single_region = constant / "polyMesh";
  if (fs::exists(single_region / "points") || fs::exists(single_region / "points.gz")) {
    return single_region;
  }

  // multi-region cases store one mesh per region, with duplicated points at the
  // region interfaces; merging them is not something this reader attempts
  if (fs::is_directory(constant)) {
    std::vector<std::string> regions;
    for (const auto& entry : fs::directory_iterator(constant)) {
      if (entry.is_directory() && fs::exists(entry.path() / "polyMesh" / "points")) {
        regions.push_back(entry.path().filename().string());
      }
    }
    if (!regions.empty()) {
      std::sort(regions.begin(), regions.end());
      std::string region_list;
      for (const auto& region : regions) {
        if (!region_list.empty()) region_list += ", ";
        region_list += region;
      }
      foam_fatal_error(
        "{} is a multi-region case, with regions: {}. XDG needs a single mesh, "
        "so pass one region's polyMesh directory directly, or build a single "
        "mesh whose materials are cellZones (see the topoSet utility).",
        path, region_list);
    }
  }

  foam_fatal_error(
    "Could not find a polyMesh in {}. Expected either {}/points or {}/points.",
    path, path, single_region.string());
}

//! Open one polyMesh file, rejecting compressed variants with an actionable message
FoamTokenStream open_mesh_file(const fs::path& dir, const std::string& name)
{
  const fs::path file = dir / name;
  if (!fs::exists(file)) {
    if (fs::exists(dir / (name + ".gz"))) {
      foam_fatal_error(
        "{} is compressed. Compressed polyMesh files are not supported; "
        "decompress them with gunzip, or set 'writeCompression off;' in "
        "system/controlDict and run 'foamFormatConvert -constant'.",
        (dir / (name + ".gz")).string());
    }
    foam_fatal_error("Missing polyMesh file {}", file.string());
  }
  return FoamTokenStream(file.string());
}

//! Cross-check the parsed mesh for internal consistency
void validate(const PolyMesh& mesh, const fs::path& dir)
{
  const std::string where = dir.string();
  const int32_t n_faces = mesh.num_faces();

  if (static_cast<int32_t>(mesh.owner.size()) != n_faces) {
    foam_fatal_error("{}: owner has {} entries but the mesh has {} faces",
                     where, mesh.owner.size(), n_faces);
  }
  if (static_cast<int32_t>(mesh.neighbour.size()) > n_faces) {
    foam_fatal_error("{}: neighbour has {} entries, more than the {} faces",
                     where, mesh.neighbour.size(), n_faces);
  }

  if (!mesh.face_offsets.empty() && mesh.face_offsets.front() != 0) {
    foam_fatal_error("{}: face offsets start at {} rather than 0", where,
                     mesh.face_offsets.front());
  }
  for (int32_t f = 0; f < n_faces; f++) {
    if (mesh.face_offsets[f + 1] < mesh.face_offsets[f]) {
      foam_fatal_error("{}: face {} has a negative size", where, f);
    }
    if (mesh.face_size(f) < 3) {
      foam_fatal_error("{}: face {} has {} points, fewer than the 3 a face needs",
                       where, f, mesh.face_size(f));
    }
  }
  if (!mesh.face_offsets.empty() &&
      mesh.face_offsets.back() != static_cast<int32_t>(mesh.face_points.size())) {
    foam_fatal_error("{}: face offsets end at {} but {} point indices were read",
                     where, mesh.face_offsets.back(), mesh.face_points.size());
  }

  for (const int32_t point : mesh.face_points) {
    if (point < 0 || point >= mesh.num_points()) {
      foam_fatal_error("{}: face references point {}, outside the {} points read",
                       where, point, mesh.num_points());
    }
  }

  for (const int32_t cell : mesh.owner) {
    if (cell < 0 || cell >= mesh.n_cells) {
      foam_fatal_error("{}: owner references cell {}, outside the {} cells declared",
                       where, cell, mesh.n_cells);
    }
  }
  for (const int32_t cell : mesh.neighbour) {
    if (cell < 0 || cell >= mesh.n_cells) {
      foam_fatal_error("{}: neighbour references cell {}, outside the {} cells declared",
                       where, cell, mesh.n_cells);
    }
  }

  // patches must tile the boundary faces exactly, since surfaces are built from
  // these ranges rather than from face membership tests
  int32_t expected_start = mesh.n_internal_faces;
  for (const auto& patch : mesh.patches) {
    if (patch.start_face != expected_start) {
      foam_fatal_error(
        "{}: patch '{}' starts at face {} but the previous patch ended at {}",
        where, patch.name, patch.start_face, expected_start);
    }
    if (patch.n_faces < 0) {
      foam_fatal_error("{}: patch '{}' has a negative face count", where, patch.name);
    }
    expected_start += patch.n_faces;
  }
  if (!mesh.patches.empty() && expected_start != n_faces) {
    foam_fatal_error("{}: patches cover faces up to {} but the mesh has {} faces",
                     where, expected_start, n_faces);
  }

  for (const auto& zone : mesh.cell_zones) {
    for (const int32_t cell : zone.cells) {
      if (cell < 0 || cell >= mesh.n_cells) {
        foam_fatal_error(
          "{}: cellZone '{}' references cell {}, outside the {} cells declared",
          where, zone.name, cell, mesh.n_cells);
      }
    }
  }
}

//! Compare parsed sizes against the counts a file advertised in its note entry
void check_note(const FoamNoteCounts& counts, const PolyMesh& mesh,
                const fs::path& file)
{
  auto check = [&](const char* name, int64_t declared, int64_t actual) {
    if (declared >= 0 && declared != actual) {
      foam_fatal_error("{}: header declares {} {} but {} were read",
                       file.string(), declared, name, actual);
    }
  };
  check("points", counts.n_points, mesh.num_points());
  check("faces", counts.n_faces, mesh.num_faces());
  check("internal faces", counts.n_internal_faces, mesh.n_internal_faces);
  // the cell count is taken from this note, so it is checked instead by owner
  // and neighbour staying within it and by every cell having faces
}

} // namespace

PolyMesh read_poly_mesh(const std::string& path)
{
  const fs::path dir = resolve_poly_mesh_dir(path);
  PolyMesh mesh;

  {
    FoamTokenStream ts = open_mesh_file(dir, "points");
    const FoamHeader header = read_foam_header(ts);
    mesh.points = read_point_field(ts, header);
  }

  {
    FoamTokenStream ts = open_mesh_file(dir, "faces");
    const FoamHeader header = read_foam_header(ts);
    read_face_list(ts, header, mesh.face_offsets, mesh.face_points);
  }

  FoamNoteCounts declared;
  {
    FoamTokenStream ts = open_mesh_file(dir, "owner");
    const FoamHeader header = read_foam_header(ts);
    mesh.owner = read_label_list(ts, header);
    declared = header.counts;
  }

  {
    FoamTokenStream ts = open_mesh_file(dir, "neighbour");
    const FoamHeader header = read_foam_header(ts);
    mesh.neighbour = read_label_list(ts, header);
  }

  {
    FoamTokenStream ts = open_mesh_file(dir, "boundary");
    read_foam_header(ts);
    mesh.patches = read_boundary(ts);
  }

  if (fs::exists(dir / "cellZones")) {
    FoamTokenStream ts = open_mesh_file(dir, "cellZones");
    const FoamHeader header = read_foam_header(ts);
    mesh.cell_zones = read_cell_zones(ts, header);
  }

  // some writers, cfMesh among them, pad neighbour with -1 to the length of
  // owner. OpenFOAM accepts that for backward compatibility by cutting the list
  // at the first -1, so the same is done here.
  if (mesh.neighbour.size() == mesh.owner.size()) {
    mesh.neighbour.erase(std::find(mesh.neighbour.begin(), mesh.neighbour.end(), -1),
                         mesh.neighbour.end());
  }
  mesh.n_internal_faces = static_cast<int32_t>(mesh.neighbour.size());

  // cells are implicit, so the count comes from the header where it is
  // advertised and is otherwise inferred from the highest cell referenced
  if (declared.n_cells >= 0) {
    mesh.n_cells = static_cast<int32_t>(declared.n_cells);
  } else {
    int32_t highest = -1;
    for (const int32_t cell : mesh.owner) highest = std::max(highest, cell);
    for (const int32_t cell : mesh.neighbour) highest = std::max(highest, cell);
    mesh.n_cells = highest + 1;
  }

  validate(mesh, dir);
  check_note(declared, mesh, dir / "owner");

  return mesh;
}

} // namespace xdg
