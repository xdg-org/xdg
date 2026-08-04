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
static constexpr const char *CLASS_ID_TAG = "class_id";

static constexpr int VERTS_PER_TET = 4;
static constexpr int VERTS_PER_TRI = 3;
static constexpr int FACES_PER_TET = 4;

static constexpr int kLocalFaceVerts[FACES_PER_TET][VERTS_PER_TRI] = {
    {0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};

// Constructor

namespace {
// Omega_h::Library wraps process-wide MPI/Kokkos init and finalize calls and
// must only be constructed once per process -- Kokkos aborts (and corrupts
// its allocator state) if initialized more than once.
Omega_h::Library &shared_omega_h_library() {
  static Omega_h::Library lib(nullptr, nullptr);
  return lib;
}
} // namespace

OmegaHMeshManager::OmegaHMeshManager() {
  mesh_ = std::make_unique<Omega_h::Mesh>(&shared_omega_h_library());
}

void OmegaHMeshManager::load_file(const std::string &file_path) {
  const int exodus_file = Omega_h::exodus::open(file_path);
  Omega_h::exodus::read_mesh(exodus_file, mesh_.get());
  Omega_h::exodus::close(exodus_file);
}

void OmegaHMeshManager::init() {
  // XDG operates on 3-dimensional volume meshes
  if (mesh()->dim() != 3) {
    fatal_error("Mesh must be 3-dimensional");
  }

  // in Omega_h the regions are 3D simplices or aka elements
  num_elements_ = mesh_->nregions();

  // We should cache all necessary derived objects as rediscovering those thing
  // during run time causes performance penalties.
  cache_derived_arrays();

  // a classified mesh defines its volumes and surfaces directly through the
  // class_dim/class_id tags. Otherwise, treat the entire mesh as a single
  // volume bounded by its exposed faces.
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

void OmegaHMeshManager::cache_derived_arrays() {
  coords_ = mesh_->coords();
  elem_verts_ = mesh_->ask_elem_verts();
  face_verts_ = mesh_->ask_verts_of(OMEGA_H_FACE);
  region_to_face_ = mesh_->ask_down(OMEGA_H_REGION, OMEGA_H_FACE);
  face_to_region_ = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
}

bool OmegaHMeshManager::has_classification() const {
  return mesh_->has_tag(OMEGA_H_REGION, CLASS_DIM_TAG) &&
         mesh_->has_tag(OMEGA_H_REGION, CLASS_ID_TAG) &&
         mesh_->has_tag(OMEGA_H_FACE, CLASS_DIM_TAG) &&
         mesh_->has_tag(OMEGA_H_FACE, CLASS_ID_TAG);
}

void OmegaHMeshManager::discover_geometry() {
  // volumes are the unique class IDs of regions classified on a 3D model entity
  auto region_class_dim =
      mesh_->get_array<Omega_h::I8>(OMEGA_H_REGION, CLASS_DIM_TAG);
  auto region_class_id =
      mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

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
    if (face_class_dim.get(face) == CLASS_DIM_SURFACE)
      surface_ids.insert(static_cast<MeshID>(face_class_id.get(face)));
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
  
  auto face_class_dim  = mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
  auto face_class_id   = mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);
  auto region_class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

  for (auto surface : surfaces_) {
    surface_senses_[surface] = {ID_NONE, ID_NONE};
  }

  for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
    if (face_class_dim.get(face) != CLASS_DIM_SURFACE)
      continue;

    MeshID surface = static_cast<MeshID>(face_class_id.get(face));
    auto &senses = surface_senses_[surface];

    Omega_h::LO f_base = face * VERTS_PER_TRI;
    std::array<Omega_h::LO, VERTS_PER_TRI> natural_order = {
      face_verts_.get(f_base + 0), face_verts_.get(f_base + 1),
        face_verts_.get(f_base + 2)
    };

    Omega_h::LO begin = face_to_region_.a2ab.get(face);
    Omega_h::LO end = face_to_region_.a2ab.get(face + 1);
    for (Omega_h::LO k = begin; k < end; ++k) {
      Omega_h::LO region = face_to_region_.ab2b.get(k);
      MeshID volume = static_cast<MeshID>(region_class_id.get(region));

      // find which of the region's 4 local faces this global face is
      Omega_h::LO rbase = region * FACES_PER_TET;
      int local_face = -1;
      for (int lf = 0; lf < FACES_PER_TET; ++lf) {
        if (region_to_face_.ab2b.get(rbase + lf) == face) {
          local_face = lf;
          break;
        }
      }

      Omega_h::LO ebase = region * VERTS_PER_TET;
      std::array<Omega_h::LO, VERTS_PER_TRI> outward_order;
      for (int i = 0; i < VERTS_PER_TRI; ++i) {
        outward_order[i] =
            elem_verts_.get(ebase + kLocalFaceVerts[local_face][i]);
      }

      // the two possible windings of 3 vertices are cyclic rotations of
      // either `outward_order` or its reverse; find where outward_order[0]
      // falls in natural_order and compare the next element to tell them apart
      int rot = 0;
      while (natural_order[rot] != outward_order[0]) {
        ++rot;
      }
      bool is_outward = natural_order[(rot + 1) % VERTS_PER_TRI] == outward_order[1];

      // Only assign each slot once: a well-formed surface's every face
      // agrees on which side is outward, so this is a no-op after the first
      // face. Some meshes carry degenerate faces classified onto a surface
      // that borders more than two distinct volumes (not a true 2-manifold
      // interface); for those, keep whichever pair of volumes was recorded
      // first rather than letting later faces keep overwriting the senses.
      if (is_outward) {
        if (senses.first == ID_NONE)
          senses.first = volume;
      } else {
        if (senses.second == ID_NONE && senses.first != volume)
          senses.second = volume;
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
    for (Omega_h::LO face = 0; face < mesh_->nfaces(); ++face) {
      Omega_h::LO n_adjacent =
          face_to_region_.a2ab.get(face + 1) - face_to_region_.a2ab.get(face);
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
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * VERTS_PER_TET;
  std::vector<MeshID> connectivity(VERTS_PER_TET);
  for (int i = 0; i < VERTS_PER_TET; ++i) {
    connectivity[i] = static_cast<MeshID>(elem_verts_.get(base + i));
  }
  return connectivity;
}

std::vector<MeshID> OmegaHMeshManager::face_connectivity(MeshID face) const {
  // triangles store three vertices per face in face->vertex order
  Omega_h::LO base = static_cast<Omega_h::LO>(face) * VERTS_PER_TRI;
  std::vector<MeshID> connectivity(VERTS_PER_TRI);
  for (int i = 0; i < VERTS_PER_TRI; ++i) {
    connectivity[i] = static_cast<MeshID>(face_verts_.get(base + i));
  }
  return connectivity;
}

MeshID OmegaHMeshManager::get_boundary_face_element(MeshID face) const {
  // the owning element of a boundary face is its single adjacent region
  Omega_h::LO begin = face_to_region_.a2ab.get(face);
  Omega_h::LO end = face_to_region_.a2ab.get(face + 1);
  if (begin == end) {
    return ID_NONE;
  }
  return static_cast<MeshID>(face_to_region_.ab2b.get(begin));
}

Vertex OmegaHMeshManager::vertex_coordinates(MeshID vertex) const {
  // coords_ is a flat array laid out as [x0, y0, z0, x1, y1, z1, ...]
  Omega_h::LO base = static_cast<Omega_h::LO>(vertex) * 3;
  return {coords_.get(base), coords_.get(base + 1), coords_.get(base + 2)};
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
  auto connectivity = element_connectivity(element);
  std::array<Vertex, 3> vertices;
  for (int i = 0; i < VERTS_PER_TRI; ++i) {
    vertices[i] =
        vertex_coordinates(connectivity[kLocalFaceVerts[local_face][i]]);
  }
  return vertices;
}

MeshID OmegaHMeshManager::adjacent_element(MeshID element, int face) const {
  // resolve the global face index for the requested local face of the element
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * FACES_PER_TET;
  Omega_h::LO global_face = region_to_face_.ab2b.get(base + face);

  // the neighbor is the other region sharing that face, if any
  Omega_h::LO begin = face_to_region_.a2ab.get(global_face);
  Omega_h::LO end = face_to_region_.a2ab.get(global_face + 1);
  for (Omega_h::LO k = begin; k < end; ++k) {
    MeshID neighbor = static_cast<MeshID>(face_to_region_.ab2b.get(k));
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