#include "xdg/openfoam/mesh_manager.h"

#include <algorithm>
#include <numeric>

#include "xdg/openfoam/foam_error.h"
#include "xdg/openfoam/cell_shape.h"
#include "xdg/util/str_utils.h"

namespace xdg {

namespace {

//! Whether \p type begins with \p prefix
//!
//! Used for the patch families rather than their individual members, so that
//! cyclicAMI and processorCyclic are caught alongside cyclic and processor.
bool has_prefix(const std::string& type, const std::string& prefix)
{
  return type.rfind(prefix, 0) == 0;
}

} // namespace

OpenFOAMMeshManager::OpenFOAMMeshManager() : MeshManager() {}

void OpenFOAMMeshManager::load_file(const std::string& filepath)
{
  mesh_ = read_poly_mesh(filepath);
}

void OpenFOAMMeshManager::init()
{
  if (mesh_.n_cells == 0) {
    foam_fatal_error("No OpenFOAM mesh has been loaded");
  }

  // checked first so that a case XDG cannot represent is refused before any
  // work is done on it, and so that the note about a two-dimensional case
  // appears before anything else it might print
  check_patch_types();

  topology_ = build_topology(mesh_);

  volumes_.clear();
  volumes_.reserve(topology_.volumes.size());
  for (const auto& volume : topology_.volumes) volumes_.push_back(volume.id);

  surfaces_.clear();
  surfaces_.reserve(topology_.surfaces.size());
  for (const auto& surface : topology_.surfaces) surfaces_.push_back(surface.id);

  build_id_maps();
  build_element_connectivity();
  check_face_and_element_types();

  // boundary surfaces have no volume on their outward side until now
  create_implicit_complement();
}

void OpenFOAMMeshManager::build_id_maps()
{
  // OpenFOAM numbers cells and points contiguously from zero, so each of these
  // collapses to a single block
  std::vector<MeshID> cells(static_cast<size_t>(mesh_.n_cells));
  std::iota(cells.begin(), cells.end(), 0);
  volume_element_id_map_ = IDBlockMapping<MeshID>(cells);

  std::vector<MeshID> points(static_cast<size_t>(mesh_.num_points()));
  std::iota(points.begin(), points.end(), 0);
  vertex_id_map_ = IDBlockMapping<MeshID>(points);
}

void OpenFOAMMeshManager::build_element_connectivity()
{
  element_type_.assign(static_cast<size_t>(mesh_.n_cells), VolumeElementType::UNKNOWN);
  element_node_offsets_.assign(static_cast<size_t>(mesh_.n_cells) + 1, 0);
  element_nodes_.clear();
  element_nodes_.reserve(static_cast<size_t>(mesh_.n_cells) * 8);

  std::vector<int32_t> nodes;
  int32_t n_unsupported = 0;
  int32_t first_unsupported = -1;
  for (int32_t cell = 0; cell < mesh_.n_cells; cell++) {
    const VolumeElementType type = cell_connectivity(mesh_, topology_, cell, nodes);
    if (type == VolumeElementType::UNSUPPORTED) {
      if (n_unsupported++ == 0) first_unsupported = cell;
      continue;
    }
    element_type_[cell] = type;
    element_nodes_.insert(element_nodes_.end(), nodes.begin(), nodes.end());
    element_node_offsets_[cell + 1] = static_cast<int32_t>(element_nodes_.size());
  }

  // reported once with a count, so a user sees how much of the mesh is affected
  if (n_unsupported > 0) {
    foam_fatal_error(
      "Cell {} is a {}. XDG supports only tetrahedra and hexahedra, and {} of "
      "the {} cells in this mesh are neither. Polyhedral and split-hexahedral "
      "cells like these are common in meshes from snappyHexMesh, polyDualMesh "
      "and partial refinement.",
      first_unsupported, cell_shape_description(mesh_, topology_, first_unsupported),
      n_unsupported, mesh_.n_cells);
  }
}

void OpenFOAMMeshManager::check_face_and_element_types() const
{
  for (const auto& surface : topology_.surfaces) {
    if (surface.faces.empty()) {
      foam_fatal_error("Surface {} has no faces, which is not supported", surface.id);
    }

    const int32_t expected = mesh_.face_size(surface.faces.front());
    for (const int32_t face : surface.faces) {
      if (mesh_.face_size(face) != expected) {
        const std::string what =
          surface.name.empty()
            ? fmt::format("the interface between volumes {} and {}", surface.forward,
                          surface.reverse)
            : fmt::format("patch '{}'", surface.name);
        foam_fatal_error(
          "Surface {} ({}) mixes faces with {} and {} vertices, which is not "
          "supported. Each surface must be entirely triangular or entirely "
          "quadrilateral.",
          surface.id, what, expected, mesh_.face_size(face));
      }
    }
  }

  for (const auto& volume : topology_.volumes) {
    if (volume.cells.empty()) {
      foam_fatal_error("Volume {} has no cells, which is not supported", volume.id);
    }

    const VolumeElementType expected = element_type_[volume.cells.front()];
    for (const int32_t cell : volume.cells) {
      if (element_type_[cell] != expected) {
        foam_fatal_error(
          "Volume {} mixes {} and {} elements, which is not supported. Each "
          "cellZone must contain a single element type.",
          volume.id, VOLUME_ELEMENT_TYPE_TO_STR.at(expected),
          VOLUME_ELEMENT_TYPE_TO_STR.at(element_type_[cell]));
      }
    }
  }
}

bool OpenFOAMMeshManager::one_cell_thick(const std::string& type) const
{
  std::vector<int32_t> faces_on_type(static_cast<size_t>(mesh_.n_cells), 0);
  for (const auto& patch : mesh_.patches) {
    if (patch.type != type) continue;
    for (int32_t f = patch.start_face; f < patch.start_face + patch.n_faces; f++) {
      faces_on_type[mesh_.owner[f]]++;
    }
  }
  return std::all_of(faces_on_type.begin(), faces_on_type.end(),
                     [](int32_t n) { return n == 2; });
}

// symmetry and symmetryPlane say directly that the model continues unchanged
// on the other side, which is a reflecting boundary. empty and wedge say the
// same of a two-dimensional or axisymmetric case, whose geometry is invariant
// along the collapsed direction, but only for a mesh that really is one cell
// thick there.
bool OpenFOAMMeshManager::implies_reflecting_boundary(const PolyMeshPatch& patch) const
{
  return patch.type == "symmetry" || patch.type == "symmetryPlane" ||
         (patch.type == "empty" && two_dimensional_) ||
         (patch.type == "wedge" && axisymmetric_);
}

void OpenFOAMMeshManager::check_patch_types()
{
  std::vector<std::string> empty_patches;
  std::vector<std::string> wedge_patches;
  auto join = [](const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) out += (out.empty() ? "" : ", ") + name;
    return out;
  };
  auto patches_have = [&](const std::vector<std::string>& names) {
    return names.size() == 1 ? "Patch " + names[0] + " has"
                             : "Patches " + join(names) + " have";
  };

  for (const auto& patch : mesh_.patches) {
    if (patch.n_faces == 0) continue;

    if (patch.type == "empty") empty_patches.push_back("'" + patch.name + "'");
    if (patch.type == "wedge") wedge_patches.push_back("'" + patch.name + "'");

    if (has_prefix(patch.type, "cyclic")) {
      foam_fatal_error(
        "Patch '{}' has type '{}', a periodic boundary. XDG has no equivalent, "
        "so the geometry would be treated as ending at this patch rather than "
        "continuing through it. Model the full domain, or replace the cyclic "
        "pair with ordinary patches.",
        patch.name, patch.type);
    }

    if (has_prefix(patch.type, "processor")) {
      foam_fatal_error(
        "Patch '{}' has type '{}', which exists only in a decomposed case and "
        "describes a partition boundary rather than a physical one. Rebuild the "
        "single-domain mesh with reconstructParMesh, or point XDG at the "
        "undecomposed constant/polyMesh.",
        patch.name, patch.type);
    }
  }

  // a two-dimensional or axisymmetric case is a real geometry, not a broken
  // one: the model is invariant along the collapsed direction, and reflecting
  // boundaries on these patches record the infinite extent it stands for.
  // Worth saying once, since it is a substantial reading of what the mesh means.
  two_dimensional_ = !empty_patches.empty() && one_cell_thick("empty");
  axisymmetric_ = !wedge_patches.empty() && one_cell_thick("wedge");

  if (two_dimensional_) {
    warning(fmt::format(
      "This is a two-dimensional OpenFOAM case (empty patches {}). It is treated "
      "as infinite along the collapsed direction, with reflecting boundaries on "
      "those patches; give a patch a 'boundary:' name or group to say otherwise.",
      join(empty_patches)));
  } else if (!empty_patches.empty()) {
    warning(fmt::format(
      "{} type 'empty', but the mesh is not one cell thick there, so this is "
      "not a two-dimensional case; blockMesh gives that type to its default "
      "patch. No boundary condition is set; use a 'boundary:' name or group to "
      "set one.",
      patches_have(empty_patches)));
  }
  if (axisymmetric_) {
    warning(fmt::format(
      "This is an axisymmetric OpenFOAM case (wedge patches {}). It is treated "
      "as invariant about the axis, with reflecting boundaries on those patches; "
      "give a patch a 'boundary:' name or group to say otherwise. Cells meeting "
      "the axis are usually degenerate and are not supported.",
      join(wedge_patches)));
  } else if (!wedge_patches.empty()) {
    warning(fmt::format(
      "{} type 'wedge', but the mesh is not one cell thick there, so this is "
      "not an axisymmetric case. No boundary condition is set; use a "
      "'boundary:' name or group to set one.",
      patches_have(wedge_patches)));
  }
}

const PolyMeshVolume* OpenFOAMMeshManager::find_volume(MeshID volume) const
{
  if (volume < 1 || volume > static_cast<MeshID>(topology_.volumes.size())) {
    return nullptr; // the implicit complement, or an unknown ID
  }
  return &topology_.volumes[volume - 1];
}

int OpenFOAMMeshManager::num_volume_elements(MeshID volume) const
{
  const PolyMeshVolume* found = find_volume(volume);
  return found ? static_cast<int>(found->cells.size()) : 0;
}

std::vector<MeshID> OpenFOAMMeshManager::get_volume_elements(MeshID volume) const
{
  const PolyMeshVolume* found = find_volume(volume);
  if (found == nullptr) return {};
  return std::vector<MeshID>(found->cells.begin(), found->cells.end());
}

int OpenFOAMMeshManager::num_surface_faces(MeshID surface) const
{
  return static_cast<int>(topology_.surface(surface).faces.size());
}

std::vector<MeshID> OpenFOAMMeshManager::get_surface_faces(MeshID surface) const
{
  const auto& faces = topology_.surface(surface).faces;
  return std::vector<MeshID>(faces.begin(), faces.end());
}

int OpenFOAMMeshManager::num_volume_faces(MeshID volume) const
{
  int count = 0;
  for (const MeshID surface : get_volume_surfaces(volume)) {
    count += num_surface_faces(surface);
  }
  return count;
}

Vertex OpenFOAMMeshManager::vertex_coordinates(MeshID vertex) const
{
  if (vertex < 0 || vertex >= mesh_.num_points()) {
    foam_fatal_error("Invalid vertex ID {} in vertex_coordinates", vertex);
  }
  return mesh_.points[vertex];
}

std::vector<MeshID> OpenFOAMMeshManager::element_connectivity(MeshID element) const
{
  if (element < 0 || element >= mesh_.n_cells) {
    foam_fatal_error("Invalid element ID {} in element_connectivity", element);
  }
  const int32_t begin = element_node_offsets_[element];
  const int32_t end = element_node_offsets_[element + 1];
  return std::vector<MeshID>(element_nodes_.begin() + begin,
                             element_nodes_.begin() + end);
}

std::vector<Vertex> OpenFOAMMeshManager::element_vertices(MeshID element) const
{
  if (element < 0 || element >= mesh_.n_cells) {
    foam_fatal_error("Invalid element ID {} in element_vertices", element);
  }
  const int32_t begin = element_node_offsets_[element];
  const int32_t end = element_node_offsets_[element + 1];

  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(end - begin));
  for (int32_t i = begin; i < end; i++) {
    vertices.push_back(mesh_.points[element_nodes_[i]]);
  }
  return vertices;
}

std::vector<MeshID> OpenFOAMMeshManager::face_vertices(MeshID face) const
{
  if (face < 0 || face >= mesh_.num_faces()) {
    foam_fatal_error("Invalid face ID {} in face_vertices", face);
  }
  const int32_t n = mesh_.face_size(face);
  std::vector<MeshID> vertices(mesh_.face_vertices(face), mesh_.face_vertices(face) + n);

  // wound so the normal points out of the surface's forward volume
  if (topology_.face_flipped[face]) {
    std::reverse(vertices.begin(), vertices.end());
  }
  return vertices;
}

std::vector<MeshID> OpenFOAMMeshManager::face_connectivity(MeshID face) const
{
  return face_vertices(face);
}

MeshID OpenFOAMMeshManager::get_boundary_face_element(MeshID face) const
{
  if (face < 0 || face >= mesh_.num_faces()) return ID_NONE;

  // the cell on the side the normal points away from, so that a ray crossing
  // into the mesh starts from the element it is entering
  return topology_.face_flipped[face] ? mesh_.neighbour[face] : mesh_.owner[face];
}

MeshID OpenFOAMMeshManager::adjacent_element(MeshID element, int face) const
{
  if (element < 0 || element >= mesh_.n_cells) return ID_NONE;
  if (face < 0 || face >= topology_.num_cell_faces(element)) return ID_NONE;

  const int32_t global_face = topology_.cell_face_list(element)[face];
  if (global_face >= mesh_.n_internal_faces) return ID_NONE; // on the boundary

  return mesh_.owner[global_face] == element ? mesh_.neighbour[global_face]
                                             : mesh_.owner[global_face];
}

SurfaceFaceType OpenFOAMMeshManager::get_surface_face_type(MeshID surface) const
{
  const auto& faces = topology_.surface(surface).faces;
  if (faces.empty()) return SurfaceFaceType::UNKNOWN;

  // the types were checked for consistency during init, so the first face
  // speaks for the whole surface
  return face_type(faces.front());
}

VolumeElementType OpenFOAMMeshManager::get_volume_element_type(MeshID volume) const
{
  const PolyMeshVolume* found = find_volume(volume);
  if (found == nullptr || found->cells.empty()) return VolumeElementType::UNKNOWN;
  return element_type(found->cells.front());
}

SurfaceFaceType OpenFOAMMeshManager::face_type(MeshID face) const
{
  if (face < 0 || face >= mesh_.num_faces()) return SurfaceFaceType::UNKNOWN;

  // every face of an accepted mesh belongs to a tet or a hex, since init
  // refuses any other cell shape
  switch (mesh_.face_size(face)) {
    case 3: return SurfaceFaceType::TRI;
    case 4: return SurfaceFaceType::QUAD;
    default: return SurfaceFaceType::UNSUPPORTED;
  }
}

VolumeElementType OpenFOAMMeshManager::element_type(MeshID element) const
{
  if (element < 0 || element >= mesh_.n_cells) return VolumeElementType::UNKNOWN;
  return element_type_[element];
}

std::vector<MeshID> OpenFOAMMeshManager::get_volume_surfaces(MeshID volume) const
{
  std::vector<MeshID> surfaces;
  for (const auto& surface : topology_.surfaces) {
    if (surface.forward == volume || surface.reverse == volume) {
      surfaces.push_back(surface.id);
    }
  }
  return surfaces;
}

std::pair<MeshID, MeshID> OpenFOAMMeshManager::surface_senses(MeshID surface) const
{
  const auto& found = topology_.surface(surface);
  return {found.forward, found.reverse};
}

Sense OpenFOAMMeshManager::surface_sense(MeshID surface, MeshID volume) const
{
  return volume == surface_senses(surface).first ? Sense::FORWARD : Sense::REVERSE;
}

MeshID OpenFOAMMeshManager::create_volume()
{
  return next_volume_id();
}

void OpenFOAMMeshManager::add_surface_to_volume(MeshID volume, MeshID surface,
                                                Sense sense, bool overwrite)
{
  if (surface < 1 || surface > static_cast<MeshID>(topology_.surfaces.size())) {
    foam_fatal_error("Invalid surface ID {} in add_surface_to_volume", surface);
  }
  PolyMeshSurface& found = topology_.surfaces[surface - 1];

  if (sense == Sense::FORWARD) {
    if (!overwrite && found.forward != ID_NONE) {
      foam_fatal_error("Surface {} already has a forward sense", surface);
    }
    found.forward = volume;
  } else {
    if (!overwrite && found.reverse != ID_NONE) {
      foam_fatal_error("Surface {} already has a reverse sense", surface);
    }
    found.reverse = volume;
  }
}

void OpenFOAMMeshManager::parse_metadata()
{
  // cellZone names carry the material, matching the convention the libMesh
  // backend applies to subdomain names
  for (const auto& volume : topology_.volumes) {
    std::string name = volume.name;
    remove_substring(name, "mat:");
    if (name.empty()) {
      volume_metadata_[{volume.id, PropertyType::MATERIAL}] = VOID_MATERIAL;
    } else {
      volume_metadata_[{volume.id, PropertyType::MATERIAL}] =
        {PropertyType::MATERIAL, name};
    }
  }

  // a boundary condition comes only from a patch named boundary:<condition>, or
  // in a group of that name, as it does from a DAGMC group name. Ordinary patch
  // names such as inlet, walls or defaultFaces label the CFD model, not a
  // transport condition, so they set none and the surface reads as transmission.
  // Patch types describing a plane the geometry is invariant across imply a
  // reflecting boundary unless a boundary: name or group says otherwise. A patch
  // split across volumes yields several surfaces, and each takes the condition
  // from the patch.
  std::vector<std::string> mirrored;
  for (const auto& surface : topology_.surfaces) {
    if (!surface.is_boundary()) continue;
    const auto& patch = mesh_.patches[surface.patch_index];

    // the patch's own name first, then a group it belongs to: adding a group
    // in constant/polyMesh/boundary leaves the case's field files alone, where
    // renaming the patch would not
    const std::string prefix = "boundary:";
    std::string condition;
    if (patch.name.rfind(prefix, 0) == 0) {
      condition = patch.name.substr(prefix.size());
    } else {
      for (const auto& group : patch.groups) {
        if (group.rfind(prefix, 0) != 0) continue;
        const std::string stated = group.substr(prefix.size());
        if (!condition.empty() && condition != stated) {
          foam_fatal_error("Patch '{}' is in groups for two boundary conditions, "
                           "'{}' and '{}'", patch.name, condition, stated);
        }
        condition = stated;
      }
    }
    if (condition.empty() && implies_reflecting_boundary(patch)) {
      condition = "reflecting";
      const std::string quoted = "'" + patch.name + "'";
      const bool symmetry = patch.type == "symmetry" || patch.type == "symmetryPlane";
      const bool listed =
        std::find(mirrored.begin(), mirrored.end(), quoted) != mirrored.end();
      if (symmetry && !listed) mirrored.push_back(quoted);
    }
    if (condition.empty()) continue;

    surface_metadata_[{surface.id, PropertyType::BOUNDARY_CONDITION}] =
      {PropertyType::BOUNDARY_CONDITION, condition};
  }

  // a reflecting boundary taken from a symmetry type is said out loud: it holds
  // only when the neutronics model shares the CFD model's symmetry, and a
  // half-channel CFD mesh inside a full-core model should transmit there.
  // Two-dimensional and axisymmetric cases already have notes of their own.
  if (!mirrored.empty()) {
    std::string names;
    for (const auto& name : mirrored) names += (names.empty() ? "" : ", ") + name;
    const std::string what =
      mirrored.size() == 1
        ? "Patch " + names + " has a symmetry type, so it is given a reflecting "
          "boundary condition."
        : "Patches " + names + " have a symmetry type, so they are given "
          "reflecting boundary conditions.";
    warning(what + " To set another condition, add the patch to a "
                   "'boundary:<condition>' group or rename it so.");
  }
}

std::vector<Vertex> OpenFOAMElementFaceAccessor::face_vertices(int i) const
{
  const PolyMesh& mesh = mesh_manager_->poly_mesh();
  const int32_t face = mesh_manager_->cell_faces(element_)[i];
  const int32_t n = mesh.face_size(face);

  std::vector<Vertex> coordinates;
  coordinates.reserve(static_cast<size_t>(n));
  for (int32_t j = 0; j < n; j++) {
    coordinates.push_back(mesh.points[mesh.face_vertices(face)[j]]);
  }

  // the stored winding points out of the owner, so it needs reversing only when
  // this element is the cell on the other side
  if (mesh.owner[face] != element_) {
    std::reverse(coordinates.begin(), coordinates.end());
  }
  return coordinates;
}

} // namespace xdg
