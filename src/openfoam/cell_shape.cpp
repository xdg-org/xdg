#include "xdg/openfoam/cell_shape.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>

#include "xdg/openfoam/foam_error.h"
#include "xdg/vec3da.h"

namespace xdg {

namespace {

//! The faces of a hexahedron in terms of the canonical vertex ordering
//!
//! This must agree with the table hex_containment_test indexes; it is the sole
//! reason a hexahedron needs its vertices ordered at all.
constexpr std::array<std::array<int, 4>, 6> HEX_FACES = {{
  {{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
  {{1, 2, 6, 5}}, {{2, 3, 7, 6}}, {{3, 0, 4, 7}}
}};

//! A cell's face, wound so that its normal points into that cell
//!
//! OpenFOAM winds every face so its normal runs from the owner into the
//! neighbour, so the stored order already points inward for a neighbour and has
//! to be reversed for an owner.
std::vector<int32_t> inward_face(const PolyMesh& mesh, int32_t cell, int32_t face)
{
  const int32_t n = mesh.face_size(face);
  std::vector<int32_t> points(mesh.face_vertices(face), mesh.face_vertices(face) + n);
  if (mesh.owner[face] == cell) {
    std::reverse(points.begin(), points.end());
  }
  return points;
}

//! Distinct vertices bounding a cell
std::set<int32_t> cell_points(const PolyMesh& mesh, const PolyMeshTopology& topology,
                              int32_t cell)
{
  std::set<int32_t> points;
  const int32_t* faces = topology.cell_face_list(cell);
  for (int32_t i = 0; i < topology.num_cell_faces(cell); i++) {
    const int32_t face = faces[i];
    for (int32_t j = 0; j < mesh.face_size(face); j++) {
      points.insert(mesh.face_vertices(face)[j]);
    }
  }
  return points;
}

//! Vertex-to-vertex adjacency along the cell's edges
std::map<int32_t, std::set<int32_t>> cell_edges(const PolyMesh& mesh,
                                                const PolyMeshTopology& topology,
                                                int32_t cell)
{
  std::map<int32_t, std::set<int32_t>> adjacency;
  const int32_t* faces = topology.cell_face_list(cell);
  for (int32_t i = 0; i < topology.num_cell_faces(cell); i++) {
    const int32_t face = faces[i];
    const int32_t n = mesh.face_size(face);
    const int32_t* points = mesh.face_vertices(face);
    for (int32_t j = 0; j < n; j++) {
      const int32_t a = points[j];
      const int32_t b = points[(j + 1) % n];
      adjacency[a].insert(b);
      adjacency[b].insert(a);
    }
  }
  return adjacency;
}

//! Signed volume of the parallelepiped spanned by three edges from \p origin
double orientation(const PolyMesh& mesh, int32_t origin, int32_t a, int32_t b, int32_t c)
{
  const Vertex& o = mesh.points[origin];
  return (mesh.points[a] - o).cross(mesh.points[b] - o).dot(mesh.points[c] - o);
}

//! Whether the faces implied by the canonical ordering are the cell's own faces
//!
//! Compared as unordered vertex sets, which is enough to catch any mistake in
//! the ordering while staying indifferent to winding.
bool hex_faces_match(const PolyMesh& mesh, const PolyMeshTopology& topology,
                     int32_t cell, const std::vector<int32_t>& nodes)
{
  std::set<std::array<int32_t, 4>> expected;
  for (const auto& face : HEX_FACES) {
    std::array<int32_t, 4> vertices {nodes[face[0]], nodes[face[1]],
                                     nodes[face[2]], nodes[face[3]]};
    std::sort(vertices.begin(), vertices.end());
    expected.insert(vertices);
  }

  std::set<std::array<int32_t, 4>> actual;
  const int32_t* faces = topology.cell_face_list(cell);
  for (int32_t i = 0; i < topology.num_cell_faces(cell); i++) {
    const int32_t face = faces[i];
    if (mesh.face_size(face) != 4) return false;
    std::array<int32_t, 4> vertices {mesh.face_vertices(face)[0], mesh.face_vertices(face)[1],
                                     mesh.face_vertices(face)[2], mesh.face_vertices(face)[3]};
    std::sort(vertices.begin(), vertices.end());
    actual.insert(vertices);
  }

  return expected == actual;
}

VolumeElementType build_tet(const PolyMesh& mesh, const PolyMeshTopology& topology,
                            int32_t cell, const std::set<int32_t>& points,
                            std::vector<int32_t>& nodes)
{
  // one face gives the base, wound so the fourth vertex lies on the side its
  // normal points toward, which is what makes the volume positive
  const std::vector<int32_t> base =
    inward_face(mesh, cell, topology.cell_face_list(cell)[0]);

  nodes = base;
  for (const int32_t point : points) {
    if (std::find(base.begin(), base.end(), point) == base.end()) {
      nodes.push_back(point);
    }
  }

  if (nodes.size() != 4) return VolumeElementType::UNSUPPORTED;

  if (orientation(mesh, nodes[0], nodes[1], nodes[2], nodes[3]) <= 0.0) {
    foam_fatal_error("Cell {} is an inverted or degenerate tetrahedron", cell);
  }
  return VolumeElementType::TET;
}

VolumeElementType build_hex(const PolyMesh& mesh, const PolyMeshTopology& topology,
                            int32_t cell, const std::set<int32_t>& points,
                            std::vector<int32_t>& nodes)
{
  // one face becomes vertices 0-3, wound inward so that the opposite face lies
  // on the side its normal points toward
  const std::vector<int32_t> base =
    inward_face(mesh, cell, topology.cell_face_list(cell)[0]);
  if (base.size() != 4) return VolumeElementType::UNSUPPORTED;

  const std::set<int32_t> base_set(base.begin(), base.end());
  const auto adjacency = cell_edges(mesh, topology, cell);

  // every vertex of a hexahedron has three neighbors: two around its own face
  // and one across the cell, which is the one wanted here
  std::vector<int32_t> opposite;
  for (const int32_t vertex : base) {
    const auto it = adjacency.find(vertex);
    if (it == adjacency.end()) return VolumeElementType::UNSUPPORTED;

    std::vector<int32_t> candidates;
    for (const int32_t neighbor : it->second) {
      if (base_set.count(neighbor) == 0) candidates.push_back(neighbor);
    }
    if (candidates.size() != 1) return VolumeElementType::UNSUPPORTED;
    opposite.push_back(candidates.front());
  }

  nodes = base;
  nodes.insert(nodes.end(), opposite.begin(), opposite.end());

  if (std::set<int32_t>(nodes.begin(), nodes.end()).size() != 8) {
    return VolumeElementType::UNSUPPORTED;
  }
  if (std::set<int32_t>(nodes.begin(), nodes.end()) != points) {
    return VolumeElementType::UNSUPPORTED;
  }

  // the ordering is only meaningful if it reproduces the cell's actual faces
  if (!hex_faces_match(mesh, topology, cell, nodes)) {
    return VolumeElementType::UNSUPPORTED;
  }

  // every corner, not just one: a hexahedron can be folded at one corner while
  // the others look right. Bottom corner i has edges to its two neighbors on
  // the bottom face and to the corner above it, and the top corners mirror that
  for (int i = 0; i < 4; i++) {
    const int next = (i + 1) % 4;
    const int prev = (i + 3) % 4;
    if (orientation(mesh, nodes[i], nodes[next], nodes[prev], nodes[i + 4]) <= 0.0 ||
        orientation(mesh, nodes[i + 4], nodes[prev + 4], nodes[next + 4], nodes[i]) <= 0.0) {
      foam_fatal_error("Cell {} is an inverted or degenerate hexahedron", cell);
    }
  }
  return VolumeElementType::HEX;
}

} // namespace

VolumeElementType cell_connectivity(const PolyMesh& mesh,
                                    const PolyMeshTopology& topology,
                                    int32_t cell,
                                    std::vector<int32_t>& nodes)
{
  nodes.clear();

  const int32_t n_faces = topology.num_cell_faces(cell);
  const int32_t* faces = topology.cell_face_list(cell);

  int32_t n_tri = 0;
  int32_t n_quad = 0;
  for (int32_t i = 0; i < n_faces; i++) {
    switch (mesh.face_size(faces[i])) {
      case 3: n_tri++; break;
      case 4: n_quad++; break;
      default: return VolumeElementType::UNSUPPORTED;
    }
  }

  const std::set<int32_t> points = cell_points(mesh, topology, cell);

  if (n_faces == 4 && n_tri == 4 && points.size() == 4) {
    return build_tet(mesh, topology, cell, points, nodes);
  }
  if (n_faces == 6 && n_quad == 6 && points.size() == 8) {
    return build_hex(mesh, topology, cell, points, nodes);
  }

  return VolumeElementType::UNSUPPORTED;
}

std::string cell_shape_description(const PolyMesh& mesh,
                                   const PolyMeshTopology& topology,
                                   int32_t cell)
{
  const int32_t n_faces = topology.num_cell_faces(cell);
  const int32_t* faces = topology.cell_face_list(cell);

  std::map<int32_t, int32_t> sizes;
  for (int32_t i = 0; i < n_faces; i++) sizes[mesh.face_size(faces[i])]++;

  std::string breakdown;
  for (const auto& [size, count] : sizes) {
    if (!breakdown.empty()) breakdown += ", ";
    breakdown += fmt::format("{} with {} vertices", count, size);
  }

  return fmt::format("cell with {} faces ({}) and {} vertices", n_faces, breakdown,
                     cell_points(mesh, topology, cell).size());
}

} // namespace xdg
