#ifndef _XDG_OPENFOAM_MESH_MANAGER
#define _XDG_OPENFOAM_MESH_MANAGER

#include <cstdint>
#include <string>
#include <vector>

#include "xdg/constants.h"
#include "xdg/element_face_accessor.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/openfoam/poly_mesh.h"
#include "xdg/openfoam/poly_mesh_topology.h"

namespace xdg {

//! \brief Mesh manager backed by an OpenFOAM polyMesh directory
//!
//! Unlike the other backends this one links no mesh library: it reads the file
//! format directly. That is practical because polyMesh already stores what XDG
//! builds its topology from, namely a face with a cell on each side and a
//! winding fixed relative to the owner.
//!
//! Volumes come from cellZones and surfaces from boundary patches and cellZone
//! interfaces, both derived in PolyMeshTopology. Only tetrahedra and hexahedra
//! are supported, matching what XDG's containment tests can handle.
class OpenFOAMMeshManager : public MeshManager {
public:
  //! Coordinates are used as written, in OpenFOAM's meters; an application
  //! working in other units applies its own factor, as OpenMC does with a
  //! mesh's length_multiplier.
  OpenFOAMMeshManager();

  ~OpenFOAMMeshManager() override = default;

  // Interface methods

  MeshLibrary mesh_library() const override { return MeshLibrary::OPENFOAM; }

  void load_file(const std::string& filepath) override;

  void init() override;

  void parse_metadata() override;

  int num_volumes() const override { return static_cast<int>(volumes_.size()); }

  int num_surfaces() const override { return static_cast<int>(surfaces_.size()); }

  int num_ents_of_dimension(int dim) const override
  {
    switch (dim) {
      case 3: return num_volumes();
      case 2: return num_surfaces();
      default: return 0;
    }
  }

  int num_vertices() const override { return mesh_.num_points(); }

  int num_volume_elements() const override { return mesh_.n_cells; }

  int num_volume_elements(MeshID volume) const override;

  int num_volume_faces(MeshID volume) const override;

  int num_surface_faces(MeshID surface) const override;

  std::vector<MeshID> get_volume_elements(MeshID volume) const override;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::vector<MeshID> element_connectivity(MeshID element) const override;

  std::vector<MeshID> face_vertices(MeshID face) const override;

  std::vector<MeshID> face_connectivity(MeshID face) const override;

  MeshID get_boundary_face_element(MeshID face) const override;

  Vertex vertex_coordinates(MeshID vertex) const override;

  SurfaceFaceType get_surface_face_type(MeshID surface) const override;

  VolumeElementType get_volume_element_type(MeshID volume) const override;

  SurfaceFaceType face_type(MeshID face) const override;

  VolumeElementType element_type(MeshID element) const override;

  MeshID adjacent_element(MeshID element, int face) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  MeshID create_volume() override;

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense,
                             bool overwrite = false) override;

  // Backend accessors

  const PolyMesh& poly_mesh() const { return mesh_; }

  const PolyMeshTopology& topology() const { return topology_; }

  //! \brief Faces bounding an element, indexed by local face number
  const int32_t* cell_faces(MeshID element) const
  {
    return topology_.cell_face_list(element);
  }

  int32_t num_cell_faces(MeshID element) const
  {
    return topology_.num_cell_faces(element);
  }

private:
  //! Map the contiguous cell and point ranges into XDG's index spaces
  void build_id_maps();

  //! Recognize every cell's shape and cache its canonical vertex ordering
  //!
  //! Done once, eagerly, because element_vertices() is called from inside the
  //! ray tracer's intersection callbacks and has to be constant-time and free
  //! of shared mutable state.
  void build_element_connectivity();

  //! Reject volumes and surfaces of mixed type, which XDG does not support
  void check_face_and_element_types() const;

  //! Report on patch types that change how the geometry should be read
  //!
  //! `empty` and `wedge` mark the collapsed direction of two-dimensional and
  //! axisymmetric cases. Those are real geometries, invariant along that
  //! direction, so they are accepted with reflecting boundaries and a note
  //! saying so, but only when the mesh is one cell thick between such patches:
  //! blockMesh also gives `empty` to its default patch on a 3D mesh. `cyclic`
  //! and `processor` have no correct reading at all and are refused.
  void check_patch_types();

  //! Whether every cell has exactly two faces on patches of type \p type
  bool one_cell_thick(const std::string& type) const;

  //! Whether a patch's type implies a reflecting boundary
  bool implies_reflecting_boundary(const PolyMeshPatch& patch) const;

  //! Look up a volume, returning nullptr for the implicit complement or any
  //! other ID with no cells behind it
  const PolyMeshVolume* find_volume(MeshID volume) const;

  PolyMesh mesh_;
  PolyMeshTopology topology_;

  //! Canonically ordered vertices of every cell, in compressed-row form
  std::vector<int32_t> element_node_offsets_;
  std::vector<int32_t> element_nodes_;
  std::vector<VolumeElementType> element_type_;

  //! Set by check_patch_types for genuine 2D and axisymmetric cases
  bool two_dimensional_ {false};
  bool axisymmetric_ {false};
};

//! \brief Face accessor over a cell's own faces
//!
//! The local face index is the cell's position in the topology's cell-to-face
//! list, which is the same index adjacent_element uses, so geometry and
//! adjacency stay in step.
struct OpenFOAMElementFaceAccessor : public ElementFaceAccessor {
  OpenFOAMElementFaceAccessor(const OpenFOAMMeshManager* mesh_manager, MeshID element)
    : ElementFaceAccessor(element), mesh_manager_(mesh_manager)
  {}

  int num_faces() const override
  {
    return static_cast<int>(mesh_manager_->num_cell_faces(element_));
  }

  //! \note Vertices come back wound so the face normal points out of this
  //!       element, which is what element traversal tests against.
  std::vector<Vertex> face_vertices(int i) const override;

  const OpenFOAMMeshManager* mesh_manager_;
};

} // namespace xdg

#endif // include guard
