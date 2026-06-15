#include "xdg/omega_h/mesh_manager.h"

#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "xdg/error.h"
#include "xdg/geometry/measure.h"
#include "xdg/moab/tag_conventions.h"
#include "xdg/util/str_utils.h"

#include "Omega_h_file.hpp"
#include "Omega_h_for.hpp"
#include "Omega_h_mesh.hpp"

namespace xdg {

// Omega_h dimension constants
static constexpr int OMEGA_H_VERTICES = 0;
static constexpr int OMEGA_H_EDGE = 1;
static constexpr int OMEGA_H_FACE = 2;
static constexpr int OMEGA_H_REGION = 3;

// Tag names used to identify geometric classification stored on the mesh
// These match the convention written by Omega_h's gmsh/vtk readers when
// class_id / class_dim tags are present.

static constexpr const char *CLASS_DIM_TAG = "class_dim";
static constexpr const char *CLASS_ID_TAG = "class_id";

OmegaHMeshManager::OmegaHMeshManager() {
  library_ = std::make_unique<Omega_h::Library>(nullptr, nullptr);
  mesh_ = std::make_unique<Omega_h::Mesh>(library_.get());
}

void OmegaHMeshManager::load_file(const std::string &file_path) {
  *mesh_ = Omega_h::binary::read(file_path, library_.get());
}

void OmegaHMeshManager::init() {
  if (mesh()->dim() != 3)
    fatal_error("Mesh must be 3-dimensional");

  bool has_class = mesh_->has_tag(OMEGA_H_REGION, CLASS_DIM_TAG) &&
                   mesh_->has_tag(OMEGA_H_REGION, CLASS_ID_TAG);

  if (has_class) {

    auto region_class_dim =
        mesh_->get_array<Omega_h::I8>(OMEGA_H_REGION, CLASS_DIM_TAG);
    auto region_class_id =
        mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

    std::set<MeshID> vol_set;
    for (Omega_h::LO i = 0; i < mesh_->nregions(); ++i) {
      if (region_class_dim.get(i) == 3)
        vol_set.insert(static_cast<MeshID>(region_class_id.get(i)));
    }
    for (auto v : vol_set)
      volumes_.push_back(v);

    if (mesh_->has_tag(OMEGA_H_FACE, CLASS_DIM_TAG) &&
        mesh_->has_tag(OMEGA_H_FACE, CLASS_ID_TAG)) {
      auto face_class_dim =
          mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
      auto face_class_id =
          mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);

      std::set<MeshID> surf_set;
      for (Omega_h::LO i = 0; i < mesh_->nfaces(); ++i) {
        if (face_class_dim.get(i) == 2)
          surf_set.insert(static_cast<MeshID>(face_class_id.get(i)));
      }
      for (auto s : surf_set)
        surfaces_.push_back(s);
    }

    if (!surfaces_.empty()) {
      auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
      auto face_class_dim =
          mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
      auto face_class_id =
          mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);
      auto region_class_id =
          mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);

      for (auto surf_id : surfaces_) {
        surface_senses_[surf_id] = {ID_NONE, ID_NONE};
      }

      for (Omega_h::LO fi = 0; fi < mesh_->nfaces(); ++fi) {
        if (face_class_dim.get(fi) != 2)
          continue;
        MeshID surf_id = static_cast<MeshID>(face_class_id.get(fi));

        Omega_h::LO begin = face_to_region.a2ab.get(fi);
        Omega_h::LO end = face_to_region.a2ab.get(fi + 1);
        for (Omega_h::LO k = begin; k < end; ++k) {
          Omega_h::LO ri = face_to_region.ab2b.get(k);
          MeshID vol_id = static_cast<MeshID>(region_class_id.get(ri));
          auto &senses = surface_senses_[surf_id];
          if (senses.first == ID_NONE)
            senses.first = vol_id; // forward
          else if (senses.second == ID_NONE && senses.first != vol_id)
            senses.second = vol_id; // reverse
        }
      }
    }
  } else {
    MeshID vol_id = create_volume();
    MeshID surf_id = next_surface_id_++;
    surfaces_.push_back(surf_id);
    surface_senses_[surf_id] = {vol_id, ID_NONE};
    add_surface_to_volume(vol_id, surf_id, Sense::FORWARD);
  }

  create_implicit_complement();
}

int OmegaHMeshManager::num_volumes() const {
  return static_cast<int>(mesh_->nelems());
}

int OmegaHMeshManager::num_surfaces() const {
  return static_cast<int>(surfaces_.size());
}

int OmegaHMeshManager::num_ents_of_dimension(int dim) const {
  return static_cast<int>(mesh_->nents(dim));
}

MeshID OmegaHMeshManager::create_volume() {
  MeshID vol_id = next_volume_id_++;
  volumes_.push_back(vol_id);
  return vol_id;
}

void OmegaHMeshManager::add_surface_to_volume(MeshID volume, MeshID surface,
                                              Sense sense, bool overwrite) {
  auto it = surface_senses_.find(surface);
  if (it == surface_senses_.end()) {
    surface_senses_[surface] = {ID_NONE, ID_NONE};
    it = surface_senses_.find(surface);
  }

  auto &senses = it->second;
  if (sense == Sense::FORWARD) {
    if (senses.first != ID_NONE && !overwrite)
      fatal_error("Surface {} forward sense already set", surface);
    senses.first = volume;
  } else if (sense == Sense::REVERSE) {
    if (senses.second != ID_NONE && !overwrite)
      fatal_error("Surface {} reverse sense already set", surface);
    senses.second = volume;
  } else {
    fatal_error("Invalid sense for surface {}", surface);
  }
}

int OmegaHMeshManager::num_volume_elements(MeshID volume) const {
  return static_cast<int>(get_volume_elements(volume).size());
}

int OmegaHMeshManager::num_volume_elements() const {
  // in omega_h number of regions = number of elements
  // are maybe same. weird
  return static_cast<int>(mesh_->nregions());
}

int OmegaHMeshManager::num_volume_faces(MeshID volume) const {
  int total = 0;
  for (auto surf : get_volume_surfaces(volume))
    total += num_surface_faces(surf);
  return total;
}

int OmegaHMeshManager::num_surface_faces(MeshID surface) const {
  return static_cast<int>(get_surface_faces(surface).size());
}

int OmegaHMeshManager::num_vertices() const {
  return static_cast<int>(mesh_->nverts());
}

std::vector<MeshID>
OmegaHMeshManager::get_volume_elements(MeshID volume) const {
  // Elements classified on a given volume have class_dim==3 and
  // class_id==volume
  std::vector<MeshID> result;

  auto class_dim = mesh_->get_array<Omega_h::I8>(OMEGA_H_REGION, CLASS_DIM_TAG);
  auto class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_REGION, CLASS_ID_TAG);
  for (Omega_h::LO i = 0; i < mesh_->nregions(); ++i) {
    if (class_dim.get(i) == 3 &&
        static_cast<MeshID>(class_id.get(i)) == volume) {
      result.push_back(static_cast<MeshID>(i));
    }
  }

  return result;
}

std::vector<MeshID> OmegaHMeshManager::get_surface_faces(MeshID surface) const {
  std::vector<MeshID> result;
  if (!mesh_->has_tag(OMEGA_H_FACE, CLASS_ID_TAG)) {

    auto face_to_region = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
    for (Omega_h::LO fi = 0; fi < mesh_->nfaces(); ++fi) {
      Omega_h::LO count =
          face_to_region.a2ab.get(fi + 1) - face_to_region.a2ab.get(fi);
      if (count == 1) // exposed face
        result.push_back(static_cast<MeshID>(fi));
    }
    return result;
  }

  auto class_dim = mesh_->get_array<Omega_h::I8>(OMEGA_H_FACE, CLASS_DIM_TAG);
  auto class_id = mesh_->get_array<Omega_h::LO>(OMEGA_H_FACE, CLASS_ID_TAG);
  for (Omega_h::LO fi = 0; fi < mesh_->nfaces(); ++fi) {
    if (class_dim.get(fi) == 2 &&
        static_cast<MeshID>(class_id.get(fi)) == surface)
      result.push_back(static_cast<MeshID>(fi));
  }
  return result;
}

std::vector<MeshID>
OmegaHMeshManager::element_connectivity(MeshID element) const {
  // tet vertex indices: 4 verts per tet stored in ask_elem_verts()
  auto ev2v = mesh_->ask_elem_verts(); // LOs, size = 4 * nregions
  constexpr int verts_per_tet = 4;
  std::vector<MeshID> conn(verts_per_tet);
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * verts_per_tet;
  for (int i = 0; i < verts_per_tet; ++i)
    conn[i] = static_cast<MeshID>(ev2v.get(base + i));
  return conn;
}

std::vector<MeshID> OmegaHMeshManager::face_connectivity(MeshID face) const {
  // tri vertex indices: 3 verts per triangle stored in ask_verts_of(2)
  auto fv2v = mesh_->ask_verts_of(OMEGA_H_FACE); // LOs, size = 3 * n_faces
  constexpr int verts_per_tri = 3;
  std::vector<MeshID> conn(verts_per_tri);
  Omega_h::LO base = static_cast<Omega_h::LO>(face) * verts_per_tri;
  for (int i = 0; i < verts_per_tri; ++i)
    conn[i] = static_cast<MeshID>(fv2v.get(base + i));
  return conn;
}

Vertex OmegaHMeshManager::vertex_coordinates(MeshID vertex_id) const {
  // coords() returns a flat array: [x0,y0,z0, x1,y1,z1, ...]
  auto coords = mesh_->coords();
  Omega_h::LO base = static_cast<Omega_h::LO>(vertex_id) * 3;
  return Vertex(coords.get(base), coords.get(base + 1), coords.get(base + 2));
}

std::vector<Vertex> OmegaHMeshManager::element_vertices(MeshID element) const {
  auto conn = element_connectivity(element);
  std::vector<Vertex> verts(conn.size());
  for (std::size_t i = 0; i < conn.size(); ++i)
    verts[i] = vertex_coordinates(conn[i]);
  return verts;
}

std::array<Vertex, 3> OmegaHMeshManager::face_vertices(MeshID face) const {
  auto fv2v = mesh_->ask_verts_of(OMEGA_H_FACE);
  constexpr int verts_per_tri = 3;
  Omega_h::LO base = static_cast<Omega_h::LO>(face) * verts_per_tri;
  auto coords = mesh_->coords();
  std::array<Vertex, 3> verts;
  for (int i = 0; i < verts_per_tri; ++i) {
    Omega_h::LO vid = fv2v.get(base + i);
    Omega_h::LO vbase = vid * 3;
    verts[i] =
        Vertex(coords.get(vbase), coords.get(vbase + 1), coords.get(vbase + 2));
  }
  return verts;
}

SurfaceElementType
OmegaHMeshManager::get_surface_element_type(MeshID /*surface*/) const {
  // Omega_h simplex meshes always use triangular surface elements
  return SurfaceElementType::TRI;
}

MeshID OmegaHMeshManager::adjacent_element(MeshID element, int face) const {
  // ask_up(face→region) gives us the regions adjacent to each face.
  // First find the face index for the given local face of the element,
  // then look at its upward adjacency.
  auto er2f =
      mesh_->ask_down(OMEGA_H_REGION, OMEGA_H_FACE); // region→face downward
  constexpr int faces_per_tet = 4;
  Omega_h::LO base = static_cast<Omega_h::LO>(element) * faces_per_tet;
  Omega_h::LO fi = er2f.ab2b.get(base + face); // global face index

  auto f2r = mesh_->ask_up(OMEGA_H_FACE, OMEGA_H_REGION);
  Omega_h::LO begin = f2r.a2ab.get(fi);
  Omega_h::LO end = f2r.a2ab.get(fi + 1);

  for (Omega_h::LO k = begin; k < end; ++k) {
    Omega_h::LO ri = f2r.ab2b.get(k);
    if (static_cast<MeshID>(ri) != element)
      return static_cast<MeshID>(ri);
  }
  return ID_NONE; // boundary face — no neighbor
}

double OmegaHMeshManager::element_volume(MeshID element) const {
  auto verts = element_vertices(element);
  std::array<Vertex, 4> tet_verts;
  for (int i = 0; i < 4; ++i)
    tet_verts[i] = verts[i];
  return tetrahedron_volume(tet_verts);
}

std::pair<MeshID, MeshID>
OmegaHMeshManager::surface_senses(MeshID surface) const {
  auto it = surface_senses_.find(surface);
  if (it == surface_senses_.end())
    return {ID_NONE, ID_NONE};
  return it->second;
}

std::vector<MeshID>
OmegaHMeshManager::get_volume_surfaces(MeshID volume) const {
  std::vector<MeshID> result;
  for (const auto &[surf, senses] : surface_senses_) {
    if (senses.first == volume || senses.second == volume)
      result.push_back(surf);
  }
  return result;
}

Sense OmegaHMeshManager::surface_sense(MeshID surface, MeshID volume) const {
  auto senses = surface_senses(surface);
  if (senses.first == volume)
    return Sense::FORWARD;
  if (senses.second == volume)
    return Sense::REVERSE;
  fatal_error("Volume {} is not a parent of surface {}", volume, surface);
  return Sense::UNSET;
}

} // namespace xdg