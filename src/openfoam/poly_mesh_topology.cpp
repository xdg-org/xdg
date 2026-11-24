#include "xdg/openfoam/poly_mesh_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xdg/openfoam/foam_error.h"

namespace xdg {

namespace {

//! Invert owner and neighbour into the faces bounding each cell
//!
//! Faces are visited in ascending order, so a cell's face run is sorted. That
//! ordering is what fixes the local face index used by adjacent_element and by
//! the element face accessor, and it must stay stable between them.
void build_cell_faces(const PolyMesh& mesh, PolyMeshTopology& topology)
{
  const int32_t n_cells = mesh.n_cells;
  const int32_t n_faces = mesh.num_faces();

  std::vector<int32_t> counts(static_cast<size_t>(n_cells), 0);
  for (int32_t f = 0; f < n_faces; f++) counts[mesh.owner[f]]++;
  for (int32_t f = 0; f < mesh.n_internal_faces; f++) counts[mesh.neighbour[f]]++;

  topology.cell_face_offsets.assign(static_cast<size_t>(n_cells) + 1, 0);
  for (int32_t c = 0; c < n_cells; c++) {
    topology.cell_face_offsets[c + 1] = topology.cell_face_offsets[c] + counts[c];
  }
  topology.cell_faces.assign(
    static_cast<size_t>(topology.cell_face_offsets[n_cells]), 0);

  std::vector<int32_t> cursor(topology.cell_face_offsets.begin(),
                              topology.cell_face_offsets.end() - 1);
  for (int32_t f = 0; f < n_faces; f++) {
    topology.cell_faces[cursor[mesh.owner[f]]++] = f;
    if (f < mesh.n_internal_faces) {
      topology.cell_faces[cursor[mesh.neighbour[f]]++] = f;
    }
  }

  // a cell bounded by fewer than four faces cannot enclose a volume
  for (int32_t c = 0; c < n_cells; c++) {
    if (topology.num_cell_faces(c) < 4) {
      foam_fatal_error("Cell {} is bounded by only {} faces", c, topology.num_cell_faces(c));
    }
  }
}

//! Assign every cell to a volume, one per cellZone plus one for unzoned cells
void build_volumes(const PolyMesh& mesh, PolyMeshTopology& topology)
{
  topology.cell_volume.assign(static_cast<size_t>(mesh.n_cells), ID_NONE);

  MeshID next_id = 1;
  for (const auto& zone : mesh.cell_zones) {
    // topoSet happily writes a zone that selected no cells; it describes no
    // geometry, so it is left out rather than becoming a volume with no cells
    if (zone.cells.empty()) {
      warning(fmt::format("cellZone '{}' contains no cells and is ignored.", zone.name));
      continue;
    }

    PolyMeshVolume volume;
    volume.id = next_id++;
    volume.name = zone.name;
    volume.cells.reserve(zone.cells.size());

    int32_t repeated = 0;
    for (const int32_t cell : zone.cells) {
      // a zone may list a cell twice, which checkMesh only warns about
      if (topology.cell_volume[cell] == volume.id) {
        repeated++;
        continue;
      }
      if (topology.cell_volume[cell] != ID_NONE) {
        const auto& other = topology.volumes[topology.cell_volume[cell] - 1];
        foam_fatal_error(
          "Cell {} belongs to both cellZone '{}' and cellZone '{}'. XDG needs "
          "each cell to carry a single material, so cellZones must not overlap.",
          cell, other.name, zone.name);
      }
      topology.cell_volume[cell] = volume.id;
      volume.cells.push_back(cell);
    }
    if (repeated > 0) {
      warning(fmt::format("cellZone '{}' lists {} of its cells more than once; each is "
                          "counted once.", zone.name, repeated));
    }
    topology.volumes.push_back(std::move(volume));
  }

  // cells named by no zone are collected into one unnamed volume, which carries
  // no material and is consequently treated as void
  std::vector<int32_t> unzoned;
  for (int32_t c = 0; c < mesh.n_cells; c++) {
    if (topology.cell_volume[c] == ID_NONE) unzoned.push_back(c);
  }

  if (!unzoned.empty()) {
    PolyMeshVolume volume;
    volume.id = next_id++;
    volume.cells = unzoned;
    for (const int32_t cell : unzoned) topology.cell_volume[cell] = volume.id;
    topology.volumes.push_back(std::move(volume));

    if (!mesh.cell_zones.empty()) {
      warning(fmt::format(
        "{} of {} cells belong to no cellZone and were collected into an "
        "unnamed volume, which will be treated as void.",
        unzoned.size(), mesh.n_cells));
    }
  }

  if (topology.volumes.empty()) {
    foam_fatal_error("The mesh contains no cells");
  }
}

//! Create surfaces from the boundary patches, splitting any patch that touches
//! more than one volume
//!
//! A patch is a user-facing grouping and is under no obligation to respect
//! cellZone boundaries, so one patch may contribute faces to several volumes.
//! Each such group becomes its own surface, mirroring how the libMesh backend
//! splits a sideset that spans subdomains.
void build_patch_surfaces(const PolyMesh& mesh, PolyMeshTopology& topology,
                          MeshID& next_id)
{
  for (size_t p = 0; p < mesh.patches.size(); p++) {
    const auto& patch = mesh.patches[p];

    // grouped by volume, and kept ordered by volume ID so that surface
    // numbering does not depend on the order faces happen to appear in
    std::map<MeshID, std::vector<int32_t>> by_volume;
    for (int32_t i = 0; i < patch.n_faces; i++) {
      const int32_t face = patch.start_face + i;
      by_volume[topology.cell_volume[mesh.owner[face]]].push_back(face);
    }

    const bool split = by_volume.size() > 1;
    for (auto& [volume_id, faces] : by_volume) {
      PolyMeshSurface surface;
      surface.id = next_id++;
      surface.patch_index = static_cast<int32_t>(p);
      surface.forward = volume_id;
      surface.reverse = ID_NONE; // filled in by the implicit complement
      surface.faces = std::move(faces);

      // a split patch yields several surfaces, so the volume name is appended
      // to keep them distinguishable in messages and metadata
      surface.name = patch.name;
      if (split) {
        const auto& volume = topology.volumes[volume_id - 1];
        surface.name += "/" + (volume.name.empty() ? std::to_string(volume_id)
                                                   : volume.name);
      }

      for (const int32_t face : surface.faces) {
        topology.face_surface[face] = surface.id;
        // a boundary face is wound to point out of its owner, which is the
        // forward volume by construction, so no flip is ever needed here
        topology.face_flipped[face] = false;
      }
      topology.surfaces.push_back(std::move(surface));
    }
  }
}

//! Create surfaces from the internal faces that separate two volumes
void build_interface_surfaces(const PolyMesh& mesh, PolyMeshTopology& topology,
                              MeshID& next_id)
{
  // keyed on the volume pair with the lower ID first, so that the two ways of
  // encountering an interface collapse onto one surface
  std::map<std::pair<MeshID, MeshID>, std::vector<int32_t>> interfaces;

  for (int32_t f = 0; f < mesh.n_internal_faces; f++) {
    const MeshID owner_volume = topology.cell_volume[mesh.owner[f]];
    const MeshID neighbour_volume = topology.cell_volume[mesh.neighbour[f]];
    if (owner_volume == neighbour_volume) continue; // interior to a volume

    const auto key = std::minmax(owner_volume, neighbour_volume);
    interfaces[{key.first, key.second}].push_back(f);
  }

  for (auto& [pair, faces] : interfaces) {
    PolyMeshSurface surface;
    surface.id = next_id++;
    surface.patch_index = -1;
    surface.forward = pair.first;
    surface.reverse = pair.second;
    surface.faces = std::move(faces);

    for (const int32_t face : surface.faces) {
      topology.face_surface[face] = surface.id;
      // OpenFOAM's owner ordering follows cell index, not volume, so roughly
      // half of an interface's faces point the wrong way for this surface
      topology.face_flipped[face] =
        topology.cell_volume[mesh.owner[face]] != surface.forward;
    }
    topology.surfaces.push_back(std::move(surface));
  }
}

//! Confirm the derived topology covers the mesh and is internally consistent
void validate(const PolyMesh& mesh, const PolyMeshTopology& topology)
{
  for (int32_t c = 0; c < mesh.n_cells; c++) {
    if (topology.cell_volume[c] == ID_NONE) {
      foam_fatal_error("Cell {} was not assigned to a volume", c);
    }
  }

  // every boundary face must belong to a surface, or part of the model's
  // exterior would simply be missing from the geometry
  for (int32_t f = mesh.n_internal_faces; f < mesh.num_faces(); f++) {
    if (topology.face_surface[f] == ID_NONE) {
      foam_fatal_error(
        "Boundary face {} belongs to no patch, so it would be absent from the "
        "geometry. Every boundary face must be named by a patch in "
        "constant/polyMesh/boundary.",
        f);
    }
  }

  // and every internal face between two volumes must belong to exactly one
  for (int32_t f = 0; f < mesh.n_internal_faces; f++) {
    const bool separates_volumes =
      topology.cell_volume[mesh.owner[f]] != topology.cell_volume[mesh.neighbour[f]];
    const bool has_surface = topology.face_surface[f] != ID_NONE;
    if (separates_volumes != has_surface) {
      foam_fatal_error("Internal face {} was assigned inconsistently to a surface", f);
    }
  }

  for (const auto& surface : topology.surfaces) {
    if (surface.faces.empty()) {
      foam_fatal_error("Surface {} has no faces", surface.id);
    }
    if (surface.forward == ID_NONE) {
      foam_fatal_error("Surface {} has no forward volume", surface.id);
    }
    if (surface.forward == surface.reverse) {
      foam_fatal_error("Surface {} has the same volume on both sides", surface.id);
    }
  }
}

//! The patch a boundary face belongs to
const PolyMeshPatch& patch_of(const PolyMesh& mesh, int32_t face)
{
  for (const auto& patch : mesh.patches) {
    if (face >= patch.start_face && face < patch.start_face + patch.n_faces) return patch;
  }
  foam_fatal_error("Boundary face {} belongs to no patch", face);
}

//! A grid cell for a face centroid, and its hash
using GridKey = std::array<int64_t, 3>;
struct GridKeyHash {
  size_t operator()(const GridKey& key) const
  {
    size_t hash = 0;
    for (const int64_t k : key) {
      hash ^= std::hash<int64_t> {}(k) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

//! Whether boundary faces \p a and \p b have the same vertices, to within \p tol
bool same_place(const PolyMesh& mesh, int32_t a, int32_t b, double tol)
{
  if (mesh.face_size(a) != mesh.face_size(b)) return false;
  for (int32_t i = 0; i < mesh.face_size(a); i++) {
    const Vertex& p = mesh.points[mesh.face_vertices(a)[i]];
    bool matched = false;
    for (int32_t j = 0; j < mesh.face_size(b) && !matched; j++) {
      const Vertex& q = mesh.points[mesh.face_vertices(b)[j]];
      matched = std::abs(p.x - q.x) <= tol && std::abs(p.y - q.y) <= tol &&
                std::abs(p.z - q.z) <= tol;
    }
    if (!matched) return false;
  }
  return true;
}

//! Refuse two boundary faces that occupy the same place
//!
//! Such pairs are walls of zero thickness. createBaffles leaves them, sharing
//! their points, and so does mergeOrSplitBaffles -split, or mergeMeshes of two
//! parts that touch without stitchMesh, each side then with its own copy of
//! the points. OpenFOAM is content with any of these, but XDG treats every
//! boundary face as the edge of the mesh, so a track would stop at the wall and
//! whatever lies beyond it would silently go untallied.
//!
//! Faces are compared by position, so point labels do not matter: centroids
//! are bucketed on a grid as fine as the tolerance, and a face is compared only
//! with those in the neighboring buckets.
void check_no_coincident_faces(const PolyMesh& mesh)
{
  if (mesh.points.empty()) return;
  Vertex lo = mesh.points.front();
  Vertex hi = lo;
  for (const auto& p : mesh.points) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  const double extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
  const double tol = 1e-9 * (extent > 0.0 ? extent : 1.0);

  std::unordered_map<GridKey, std::vector<int32_t>, GridKeyHash> buckets;
  for (int32_t f = mesh.n_internal_faces; f < mesh.num_faces(); f++) {
    Vertex centroid {0.0, 0.0, 0.0};
    for (int32_t i = 0; i < mesh.face_size(f); i++) {
      centroid = centroid + mesh.points[mesh.face_vertices(f)[i]];
    }
    centroid = centroid / static_cast<double>(mesh.face_size(f));
    const GridKey key {static_cast<int64_t>(std::floor((centroid.x - lo.x) / tol)),
                       static_cast<int64_t>(std::floor((centroid.y - lo.y) / tol)),
                       static_cast<int64_t>(std::floor((centroid.z - lo.z) / tol))};

    for (int64_t dx = -1; dx <= 1; dx++) {
      for (int64_t dy = -1; dy <= 1; dy++) {
        for (int64_t dz = -1; dz <= 1; dz++) {
          const auto it = buckets.find({key[0] + dx, key[1] + dy, key[2] + dz});
          if (it == buckets.end()) continue;
          for (const int32_t other : it->second) {
            if (!same_place(mesh, other, f, tol)) continue;
            foam_fatal_error(
              "Boundary faces {} (patch '{}') and {} (patch '{}') occupy the same "
              "place: the mesh has a wall of zero thickness, either baffles or two "
              "parts that touch without being stitched together. XDG treats every "
              "boundary face as the edge of the mesh, so tracks would stop there. "
              "Merge baffles back into internal faces with mergeOrSplitBaffles, and "
              "join touching parts with stitchMesh.",
              other, patch_of(mesh, other).name, f, patch_of(mesh, f).name);
          }
        }
      }
    }
    buckets[key].push_back(f);
  }
}

} // namespace

PolyMeshTopology build_topology(const PolyMesh& mesh)
{
  PolyMeshTopology topology;

  topology.face_surface.assign(static_cast<size_t>(mesh.num_faces()), ID_NONE);
  topology.face_flipped.assign(static_cast<size_t>(mesh.num_faces()), false);

  check_no_coincident_faces(mesh);
  build_cell_faces(mesh, topology);
  build_volumes(mesh, topology);

  MeshID next_surface_id = 1;
  build_patch_surfaces(mesh, topology, next_surface_id);
  build_interface_surfaces(mesh, topology, next_surface_id);

  validate(mesh, topology);

  return topology;
}

} // namespace xdg
