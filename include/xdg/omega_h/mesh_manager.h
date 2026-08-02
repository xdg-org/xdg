#ifndef XDG_OMEGA_H_MESH_MANAGER_H
#define XDG_OMEGA_H_MESH_MANAGER_H

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "xdg/constants.h"
#include "xdg/element_face_accessor.h"
#include "xdg/mesh_manager_interface.h"

#include "Omega_h_library.hpp"
#include "Omega_h_mesh.hpp"

namespace xdg {

//! \brief Mesh manager backed by the Omega_h simplex mesh library.
//!
//! Omega_h tetrahedral meshes carry the geometric model they were generated
//! from as "class_dim"/"class_id" classification tags on each entity. These
//! tags map mesh entities onto the volumes (3D model entities) and surfaces
//! (2D model entities) of the geometry, which is all the topology XDG needs.
//! When a mesh has no classification, the whole mesh is treated as a single
//! volume bounded by its exposed faces.
class OmegaHMeshManager : public MeshManager {
public:
  OmegaHMeshManager();

  OmegaHMeshManager(const Omega_h::Mesh* mesh);

  ~OmegaHMeshManager() override = default;

  // Interface methods
  MeshLibrary mesh_library() const override { return MeshLibrary::OMEGA_H; }

  void load_file(const std::string &file_path) override;

  void init() override;

  // TODO: I will have to implement this one.
  void parse_metadata() override {};

  int num_volumes() const override { return volumes_.size(); }

  int num_surfaces() const override { return surfaces_.size(); }

  int num_ents_of_dimension(int dim) const override {
    switch (dim) {
    case 3:
      return num_volumes();
    case 2:
      return num_surfaces();
    default:
      return 0;
    }
  }

  int num_volume_elements(MeshID volume) const override {
    return get_volume_elements(volume).size();
  }

  int num_volume_elements() const override { return num_elements_; }

  int num_volume_faces(MeshID volume) const override {
    int count = 0;
    for (auto surface : get_volume_surfaces(volume)) {
      count += num_surface_faces(surface);
    }
    return count;
  }

  int num_surface_faces(MeshID surface) const override {
    return get_surface_faces(surface).size();
  }

  int num_vertices() const override;

  std::vector<MeshID> get_volume_elements(MeshID volume) const override;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<MeshID> element_connectivity(MeshID element) const override;

  std::vector<MeshID> face_connectivity(MeshID face) const override;

  MeshID get_boundary_face_element(MeshID face) const override;

  Vertex vertex_coordinates(MeshID vertex) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::array<Vertex, 3> face_vertices(MeshID face) const override;

  //! \brief Vertices of a local face of a tetrahedral element
  std::array<Vertex, 3> element_face_vertices(MeshID element, int local_face) const;

  SurfaceElementType
  get_surface_element_type(MeshID surface_element_id) const override {
    // Omega_h simplex meshes always use triangular surface elements
    return SurfaceElementType::TRI;
  }

  MeshID adjacent_element(MeshID element, int face) const override;

  double element_volume(MeshID element) const override;

  MeshID create_volume() override;

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense,
                             bool overwrite = false) override;

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  // Accessors
  Omega_h::Mesh *mesh() { return mesh_.get(); }
  const Omega_h::Mesh *mesh() const { return mesh_.get(); }

private:
  //! \brief Whether the mesh carries volume and surface classification tags
  bool has_classification() const;

  //! \brief Populate volumes_ and surfaces_ from the classification tags
  void discover_geometry();

  //! \brief Treat the whole mesh as a single volume bounded by its exposed
  //! faces
  void discover_single_volume();

  //! \brief Assign the parent volumes (forward/reverse senses) of each surface
  void determine_surface_senses();

  //! \brief Map element and vertex ID spaces into contiguous index spaces
  void map_id_spaces();

  //! \brief Fetch Omega_h's derived adjacencies and coordinate array exactly
  //! once, single-threaded, and store them as members. Omega_h::Read<>/Adj
  //! objects are reference-counted handles (Omega_h::SharedAlloc) whose
  //! copy/destroy operations are NOT thread-safe. Every accessor below reads
  //! only from these cached members instead of calling mesh_->ask_*()/coords()
  //! itself, so no Read<>/Adj handle is ever copied or destroyed again after
  //! init() -- which is what let concurrent Embree bounds-callback threads
  //! race on Omega_h's internal refcounts and cause a double-free.
  void cache_derived_arrays();

  std::unique_ptr<Omega_h::Mesh> mesh_;

  //! Cached derived arrays/adjacencies (see cache_derived_arrays()). Never
  //! touch mesh_->ask_*()/coords() anywhere outside of that function -- read
  //! from these members instead so no Omega_h::Read<>/Adj handle is copied or
  //! destroyed from more than one thread.
  Omega_h::Reals coords_;
  Omega_h::LOs elem_verts_;
  Omega_h::LOs face_verts_;
  Omega_h::Adj region_to_face_;
  Omega_h::Adj face_to_region_;

  //! Mapping of surfaces to the volumes on either side. Volumes are ordered
  //! based on their sense with respect to the surface triangles
  std::map<MeshID, std::pair<MeshID, MeshID>> surface_senses_;

  int32_t num_elements_{-1};
};

//! \brief Face-vertex accessor for Omega_h tetrahedral elements
struct OmegaHElementFaceAccessor : public ElementFaceAccessor {
  OmegaHElementFaceAccessor(const OmegaHMeshManager *mesh_manager,
                            MeshID element)
      : ElementFaceAccessor(element), mesh_manager_(mesh_manager) {}

  std::array<Vertex, 3> face_vertices(int i) const override {
    return mesh_manager_->element_face_vertices(element_, i);
  }

  const OmegaHMeshManager *mesh_manager_;
};

} // namespace xdg

#endif // XDG_OMEGA_H_MESH_MANAGER_H