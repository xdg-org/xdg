#include "xdg/omega_h/mesh_manager.h"

#include <array>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

#include "xdg/error.h"
#include "xdg/geometry/measure.h"

#include "Omega_h_file.hpp"

namespace xdg {

// Topological entity dimensions in Omega_h
static constexpr int OMEGA_H_FACE = 2;
static constexpr int OMEGA_H_REGION = 3;

// Geometric model dimension a mesh entity may be classified on
static constexpr int CLASS_DIM_SURFACE = 2;
static constexpr int CLASS_DIM_VOLUME = 3;

// Classification tag names written by Omega_h
static constexpr const char *CLASS_DIM_TAG = "class_dim";
static constexpr const char *CLASS_ID_TAG  = "class_id";

static constexpr int VERTS_PER_TET = 4;
static constexpr int VERTS_PER_TRI = 3;
static constexpr int FACES_PER_TET = 4;

// Constructor

OmegaHMeshManager::OmegaHMeshManager(const Omega_h::Mesh* mesh) {

  // I will rewrite this method later
  library_ = std::make_unique<Omega_h::Library>(nullptr, nullptr);
  mesh_ = std::make_unique<Omega_h::Mesh>(library_.get());

}
OmegaHMeshManager::OmegaHMeshManager() {
  library_ = std::make_unique<Omega_h::Library>(nullptr, nullptr);
  mesh_ = std::make_unique<Omega_h::Mesh>(library_.get());
}

void OmegaHMeshManager::load_file(const std::string &file_path) {
  *mesh_ = Omega_h::binary::read(file_path, library_.get());
}

void OmegaHMeshManager::init() {
  // XDG operates on 3-dimensional volume meshes
  if (mesh()->dim() != 3) {
    fatal_error("Mesh must be 3-dimensional");
  }

  // in Omega_h the regions are 3D simplices or aka elements
  num_elements_ = mesh_->nregions();

  // a classified mesh defines its volumes and surfaces directly through the
  // class_dim/class_id tags. Otherwise, treat the entire mesh as a single volume
  // bounded by its exposed faces.
  if (has_classification()) {
    discover_geometry();
    determine_surface_senses();
  } else {
    discover_single_volume();
  }

  // create an implicit complement to bound the model
  create_implicit_complement();

  // map ID spaces into indices for ordered access by downstream applications
  map_id_spaces();
}

bool OmegaHMeshManager::has_classification() const {
  return mesh_->has_tag(OMEGA_H_REGION, CLASS_DIM_TAG) &&
         mesh_->has_tag(OMEGA_H_REGION, CLASS_ID_TAG) &&
         mesh_->has_tag(OMEGA_H_FACE, CLASS_DIM_TAG) &&
         mesh_->has_tag(OMEGA_H_FACE, CLASS_ID_TAG);
}

void OmegaHMeshManager::discover_geometry() {
  // volumes are the unique class IDs of regions classified on a 3D model entity
  auto region_class_dim = mesh_->get_array<Omega_h::I8>(OMEGA_H_REGION, CLASS_DIM_TAG);
  auto region_class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

  std::set<MeshID> volume_ids;
  for (Omega_h::LO region = 0; region < mesh_->nregions(); ++region) {
    if (region_class_dim.get(region) == CLASS_DIM_VOLUME) {
      volume_ids.insert(static_cast<MeshID>(region_class_id.get(region)));
    }
  }
  volumes_.assign(volume_ids.begin(), volume_ids.end());

  // surfaces are the unique class IDs of faces classified on a 2D model entity
  auto face_class_dim =
      mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
  auto face_class_id =
      mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);

  std::set<MeshID> surface_ids;
  for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
    if (face_class_dim.get(face) == CLASS_DIM_SURFACE) {
      surface_ids.insert(static_cast<MeshID>(face_class_id.get(face)));
    }
  }
  surfaces_.assign(surface_ids.begin(), surface_ids.end());
}

void OmegaHMeshManager::discover_single_volume() {
  // one volume bounded by a single surface made up of every exposed face
  MeshID volume = create_volume();
  volumes_.push_back(volume);

  MeshID surface = next_surface_id();
  surfaces_.push_back(surface);
  surface_senses_[surface] = {volume, ID_NONE};
}

void OmegaHMeshManager::determine_surface_senses() {
  // the parent volumes of a surface are the volumes of the regions on either
  // side of its faces. The first region encountered defines the forward sense
  // and the opposing region, if any, defines the reverse sense.
  auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
  auto face_class_dim =
      mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
  auto face_class_id =
      mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);
  auto region_class_id =
      mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

  for (auto surface : surfaces_) {
    surface_senses_[surface] = {ID_NONE, ID_NONE};
  }

  for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
    if (face_class_dim.get(face) != CLASS_DIM_SURFACE) {
      continue;
    }
    MeshID surface = static_cast<MeshID>(face_class_id.get(face));
    auto &senses = surface_senses_[surface];

    Omega_h::LO begin = face_to_region.a2ab.get(face);
    Omega_h::LO end = face_to_region.a2ab.get(face + 1);
    for (Omega_h::LO k = begin; k < end; ++k) {
      MeshID volume =
          static_cast<MeshID>(region_class_id.get(face_to_region.ab2b.get(k)));
      if (senses.first == ID_NONE) {
        senses.first = volume; // forward sense
      } else if (senses.second == ID_NONE && senses.first != volume) {
        senses.second = volume; // reverse sense
      }
    }
  }
}

void OmegaHMeshManager::map_id_spaces() {
  // Omega_h stores entities in a contiguous, zero-based index space, so element
  // and vertex IDs are identical to their indices
  std::vector<MeshID> element_ids(mesh_->nregions());
  std::iota(element_ids.begin(), element_ids.end(), 0);
  volume_element_id_map_ = IDBlockMapping<MeshID>(element_ids);

  std::vector<MeshID> vertex_ids(mesh_->nverts());
  std::iota(vertex_ids.begin(), vertex_ids.end(), 0);
  vertex_id_map_ = IDBlockMapping<MeshID>(vertex_ids);
}

MeshID OmegaHMeshManager::create_volume() { return next_volume_id(); }

void OmegaHMeshManager::add_surface_to_volume(MeshID volume, MeshID surface,
                                              Sense sense, bool overwrite) {
  auto senses = surface_senses(surface);
  if (sense == Sense::FORWARD) {
    if (!overwrite && senses.first != ID_NONE) {
      fatal_error("Surface {} already has a forward sense", surface);
    }
    surface_senses_[surface] = {volume, senses.second};
  } else {
    if (!overwrite && senses.second != ID_NONE) {
      fatal_error("Surface {} already has a reverse sense", surface);
    }
    surface_senses_[surface] = {senses.first, volume};
  }
}

int OmegaHMeshManager::num_vertices() const { return mesh_->nverts(); }

std::vector<MeshID>
OmegaHMeshManager::get_volume_elements(MeshID volume) const {
  std::vector<MeshID> elements;

  // without classification every region belongs to the single volume
  if (!mesh_->has_tag(OMEGA_H_REGION, CLASS_ID_TAG)) {
    elements.resize(mesh_->nregions());
    std::iota(elements.begin(), elements.end(), 0);
    return elements;
  }

  // otherwise gather the regions classified on this volume
  auto class_dim = mesh_->get_array<Omega_h::I8>(OMEGA_H_REGION, CLASS_DIM_TAG);
  auto class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);
  for (Omega_h::LO region = 0; region < mesh_->nregions(); ++region) {
    if (class_dim.get(region) == CLASS_DIM_VOLUME &&
        static_cast<MeshID>(class_id.get(region)) == volume) {
      elements.push_back(static_cast<MeshID>(region));
    }
  }
  return elements;
}

std::vector<MeshID> OmegaHMeshManager::get_surface_faces(MeshID surface) const {
  std::vector<MeshID> faces;

  // without classification the surface is the set of exposed boundary faces
  // (faces adjacent to exactly one region)
  if (!mesh_->has_tag(OMEGA_H_FACE, CLASS_ID_TAG)) {
    auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
    for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
      Omega_h::LO n_adjacent =
          face_to_region.a2ab.get(face + 1) - face_to_region.a2ab.get(face);
      if (n_adjacent == 1) {
        faces.push_back(static_cast<MeshID>(face));
      }
    }
    return faces;
  }

  // otherwise gather the faces classified on this surface
  auto class_dim = mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
  auto class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);
  for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
    if (class_dim.get(face) == CLASS_DIM_SURFACE &&
        static_cast<MeshID>(class_id.get(face)) == surface) {
      faces.push_back(static_cast<MeshID>(face));
    }
  }
  return faces;
}

std::vector<MeshID>
OmegaHMeshManager::element_connectivity(MeshID element) const {
  // tetrahedra store four vertices per element in element->vertex order
  auto elem_verts = mesh_->ask_elem_verts();
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * VERTS_PER_TET;
  std::vector<MeshID> connectivity(VERTS_PER_TET);
  for (int i = 0; i < VERTS_PER_TET; ++i) {
    connectivity[i] = static_cast<MeshID>(elem_verts.get(base + i));
  }
  return connectivity;
}

std::vector<MeshID> OmegaHMeshManager::face_connectivity(MeshID face) const {
  // triangles store three vertices per face in face->vertex order
  auto face_verts = mesh_->ask_verts_of(OMEGA_H_FACE);
  Omega_h::LO base = static_cast<Omega_h::LO>(face) * VERTS_PER_TRI;
  std::vector<MeshID> connectivity(VERTS_PER_TRI);
  for (int i = 0; i < VERTS_PER_TRI; ++i) {
    connectivity[i] = static_cast<MeshID>(face_verts.get(base + i));
  }
  return connectivity;
}

MeshID OmegaHMeshManager::get_boundary_face_element(MeshID face) const {
  // the owning element of a boundary face is its single adjacent region
  auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
  Omega_h::LO begin = face_to_region.a2ab.get(face);
  Omega_h::LO end = face_to_region.a2ab.get(face + 1);
  if (begin == end) {
    return ID_NONE;
  }
  return static_cast<MeshID>(face_to_region.ab2b.get(begin));
}

Vertex OmegaHMeshManager::vertex_coordinates(MeshID vertex) const {
  // coords() is a flat array laid out as [x0, y0, z0, x1, y1, z1, ...]
  auto coords = mesh_->coords();
  Omega_h::LO base = static_cast<Omega_h::LO>(vertex) * 3;
  return {coords.get(base), coords.get(base + 1), coords.get(base + 2)};
}

std::vector<Vertex> OmegaHMeshManager::element_vertices(MeshID element) const {
  auto connectivity = element_connectivity(element);
  std::vector<Vertex> vertices;
  vertices.reserve(connectivity.size());
  for (auto vertex : connectivity) {
    vertices.push_back(vertex_coordinates(vertex));
  }
  return vertices;
}

std::array<Vertex, 3> OmegaHMeshManager::face_vertices(MeshID face) const {
  auto connectivity = face_connectivity(face);
  std::array<Vertex, 3> vertices;
  for (int i = 0; i < VERTS_PER_TRI; ++i) {
    vertices[i] = vertex_coordinates(connectivity[i]);
  }
  return vertices;
}

std::array<Vertex, 3>
OmegaHMeshManager::element_face_vertices(MeshID element, int local_face) const {
  // resolve the global face index for the requested local face, then return its
  // vertices in their natural (face->vertex) orientation
  auto region_to_face = mesh_->ask_down(OMEGA_H_REGION, OMEGA_H_FACE);
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * FACES_PER_TET;
  Omega_h::LO global_face = region_to_face.ab2b.get(base + local_face);
  return face_vertices(static_cast<MeshID>(global_face));
}

MeshID OmegaHMeshManager::adjacent_element(MeshID element, int face) const {
  // resolve the global face index for the requested local face of the element
  auto region_to_face = mesh_->ask_down(OMEGA_H_REGION, OMEGA_H_FACE);
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * FACES_PER_TET;
  Omega_h::LO global_face = region_to_face.ab2b.get(base + face);

  // the neighbor is the other region sharing that face, if any
  auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
  Omega_h::LO begin = face_to_region.a2ab.get(global_face);
  Omega_h::LO end = face_to_region.a2ab.get(global_face + 1);
  for (Omega_h::LO k = begin; k < end; ++k) {
    MeshID neighbor = static_cast<MeshID>(face_to_region.ab2b.get(k));
    if (neighbor != element) {
      return neighbor;
    }
  }
  return ID_NONE; // boundary face, no neighbor
}

double OmegaHMeshManager::element_volume(MeshID element) const {
  auto vertices = element_vertices(element);
  std::array<Vertex, 4> tet{vertices[0], vertices[1], vertices[2], vertices[3]};
  return tetrahedron_volume(tet);
}

std::pair<MeshID, MeshID>
OmegaHMeshManager::surface_senses(MeshID surface) const {
  auto it = surface_senses_.find(surface);
  if (it == surface_senses_.end()) {
    return {ID_NONE, ID_NONE};
  }
  return it->second;
}

std::vector<MeshID>
OmegaHMeshManager::get_volume_surfaces(MeshID volume) const {
  // walk the surface senses and return the surfaces bounding this volume
  std::vector<MeshID> result;
  for (const auto &[surface, senses] : surface_senses_) {
    if (senses.first == volume || senses.second == volume) {
      result.push_back(surface);
    }
  }
  return result;
}

Sense OmegaHMeshManager::surface_sense(MeshID surface, MeshID volume) const {
  auto senses = surface_senses(surface);
  return volume == senses.first ? Sense::FORWARD : Sense::REVERSE;
}

} // namespace xdg