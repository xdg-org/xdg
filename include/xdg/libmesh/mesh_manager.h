#ifndef _XDG_LIBMESH_MESH_MANAGER
#define _XDG_LIBMESH_MESH_MANAGER

#include <map>
#include <memory>
#include <vector>

#include "xdg/constants.h"
#include "xdg/element_face_accessor.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/error.h"

#include "libmesh/libmesh.h"
#include "libmesh/elem.h"
#include "libmesh/cell_tet4.h"
#include "libmesh/cell_hex8.h"
#include "libmesh/mesh.h"
namespace xdg {


class LibMeshManager : public MeshManager {

  constexpr static int SIDE_NONE {-1};
  // Valid neighbors can -1 for boundary faces, so we assign -2 to mean invalid neighbors.
  constexpr static int INVALID_NEIGHBOR {-2};

public:
  LibMeshManager(const libMesh::MeshBase* ptr);

  LibMeshManager();

  ~LibMeshManager() override = default;

  // Interface methods
  MeshLibrary mesh_library() const override { return MeshLibrary::LIBMESH; }

  void load_file(const std::string& filepath) override;

  void init() override;

  void parse_metadata() override;

  static VolumeElementType _elem_xdg_type(const libMesh::Elem *elem_ptr) {
    switch (elem_ptr->type()) {
    case libMesh::TET4:
      return VolumeElementType::TET;
    case libMesh::HEX8:
      return VolumeElementType::HEX;
    default:
      return VolumeElementType::UNSUPPORTED;
    }
  }

  bool has_amr_conn() const { return amr_conn_map_.size() > 0; }

  bool is_elem_adaptive(MeshID element) const {
    return amr_conn_map_.count(element);
  }

  int get_num_subfaces(MeshID element, int face) const {
    return amr_conn_map_.at(element).get_num_subfaces(face);
  }

  int num_volumes() const override { return volumes_.size(); }

  int num_surfaces() const override { return surfaces_.size(); }

  int num_ents_of_dimension(int dim) const override {
    switch (dim) {
      case 3: return num_volumes();
      case 2: return num_surfaces();
      default: return 0;
    }
  }

  int num_volume_elements(MeshID volume) const override {
    return get_volume_elements(volume).size();
  }

  int num_volume_elements() const override;

  int num_volume_faces(MeshID volume) const override {
    int count = 0;
    for (auto s : this->get_volume_surfaces(volume)) {
      count += surface_map_.at(s).size();
    }
    return count;
  }

  int num_surface_faces(MeshID surface) const override {
    return surface_map_.at(surface).size();
  }

  int num_vertices() const override;

  std::vector<MeshID> get_volume_elements(MeshID volume) const;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::vector<MeshID> element_connectivity(MeshID element) const override;

  std::vector<MeshID> face_connectivity(MeshID element) const override;

  MeshID get_boundary_face_element(MeshID face) const override;

  Vertex vertex_coordinates(MeshID vertex) const override;

  std::vector<MeshID> face_vertices(MeshID triangle) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  SurfaceFaceType get_surface_face_type(MeshID surface) const override;

  SurfaceFaceType face_type(MeshID face) const override;

  VolumeElementType get_volume_element_type(MeshID volume) const override;

  VolumeElementType element_type(MeshID element) const override;

  MeshID adjacent_element(MeshID element, int face) const override;

  std::pair<MeshID, int> adjacent_subelement(MeshID element, int face, int subface_idx) const override;

  MeshID create_volume() override;

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override;

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  // Accessors
  const libMesh::MeshBase* mesh() const { return mesh_; }

private:
  //! Helper struct for unique identification of an element face
  struct SidePair {
    SidePair() = default;

    SidePair(std::pair<const libMesh::Elem*, int> old_side) {
      side_.first = old_side.first;
      side_num_ = old_side.second;
      side_.second = old_side.first->neighbor_ptr(side_num_);
      if (side_.second != nullptr) {
        set_order();
        set_side_num();
      }
    }

    SidePair(const libMesh::Elem* elem, int side_num) {
      side_.first = elem;
      side_num_ = side_num;
      side_.second = elem->neighbor_ptr(side_num);
      if (side_.second != nullptr) {
        set_order();
        set_side_num();
      }
    }

    SidePair(const libMesh::Elem* elem1, const libMesh::Elem* elem2) {
      side_.first = elem1;
      side_.second = elem2;
      if (elem1 == nullptr || elem2 == nullptr) {
        fatal_error("SidePair pointer constructor must be used with non-null pointers");
      }
      set_order();
      set_side_num();
    }

    SidePair(std::pair<const libMesh::Elem*, const libMesh::Elem*> side_pair) : side_(side_pair) {
      if (side_.first == nullptr || side_.second == nullptr) {
        fatal_error("SidePair pair constructor must be used with non-null pointers");
      }
      set_order();
      set_side_num();
    }

    void set_order() {
      if (side_.first == nullptr && side_.second == nullptr) {
        fatal_error("SidePair created with null elements");
      }

      // re-order so that null pointers come second if present
      if (side_.first == nullptr) {
        std::swap(side_.first, side_.second);
        return;
      }

      // if a null pointer is present, the remaining checks aren't necessary
      if (side_.second == nullptr) {
        return;
      }

      if (side_.first->id() == side_.second->id()) {
        fatal_error("SidePair created with the same element on both sides. Element ID: {}", side_.first->id());
      }

      if (side_.first->id() > side_.second->id()) {
        std::swap(side_.first, side_.second);
      }
    }

    SurfaceFaceType face_type() const {
      switch (side_.first->type()) {
        case libMesh::TET4:
          return SurfaceFaceType::TRI;
        case libMesh::HEX8:
          return SurfaceFaceType::QUAD;
        default:
          return SurfaceFaceType::UNSUPPORTED;
      }
    }

    void set_side_num() {
      reverse_winding_ = false;

      for (int i = 0; i < side_.first->n_sides(); i++) {
        if (side_.first->neighbor_ptr(i) == side_.second) {
          side_num_ = i;
          return;
        }
      }
      // If we've gotten here, we could be dealing with an AMR mesh. An element in first()
      // may be less refined then the element in second(), so we need to ensure faces are
      // quieried with second() and the face adjusted accordingly. However, we need to
      // signal that the winding order be reversed to ensure the normals are consistent.
      auto unrefined_neighbor = side_.first->which_neighbor_am_i(side_.second);
      if (unrefined_neighbor != libMesh::invalid_uint) {
        // second() is a neighbor of first()
        reverse_winding_ = true;
        side_num_ = side_.second->which_neighbor_am_i(side_.first);
        return;
      }

      if (side_num_ == SIDE_NONE) {
        fatal_error("SidePair created with elements that are not neighbors");
      }
    }

    void swap() {
      std::swap(side_.first, side_.second);
      set_side_num();
    }

    bool operator==(const SidePair& other) const
    {
      return first() == other.first() && side_num() == other.side_num();
    }

    bool operator<(const SidePair& other) const
    {
      return side_ < other.side_;
    }

    const libMesh::Elem* first() const { return side_.first; }
    const libMesh::Elem* second() const { return side_.second; }

    int32_t side_num() const { return side_num_; }
    bool reverse_winding() const { return reverse_winding_; }

    const std::unique_ptr<const libMesh::Elem> face_ptr() const {
      if (reverse_winding()) {
        return second()->side_ptr(side_num());
      } else {
        return first()->side_ptr(side_num());
      }
    }

    std::pair<const libMesh::Elem*, const libMesh::Elem*> side_ {nullptr, nullptr};
    int32_t side_num_ {SIDE_NONE};
    bool reverse_winding_ {false};
  }; // SidePair

  struct SidePairHash {

    std::size_t operator()(const SidePair& p) const
    {
      auto hash1 = std::hash<const libMesh::Elem*>{}(p.first());
      auto hash2 = std::hash<int32_t>{}(p.side_num());
      // Combine the two hashes into a single hash value
      // This can be done using a common technique that combines them with XOR and bit shifting
      return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }
  };

  MeshID sidepair_id(const SidePair& sidepair) {
    if (sidepair_to_mesh_id_.count(sidepair) == 0) {
      MeshID id = next_sidepair_id();
      mesh_id_to_sidepair_[id] = sidepair;
      sidepair_to_mesh_id_[sidepair] = id;
      return id;
    } else {
      return sidepair_to_mesh_id_[sidepair];
    }
  }

  MeshID sidepair_id(const libMesh::Elem* elem1, const libMesh::Elem* elem2) {
    return sidepair_id(SidePair(elem1, elem2));
  }

  const SidePair& sidepair(MeshID sidepair) const {
    return mesh_id_to_sidepair_.at(sidepair);
  }

  MeshID next_sidepair_id() {
    return next_sidepair_id_++;
  }

  struct MeshIDPairHash {
    std::size_t operator()(const std::pair<MeshID, MeshID>& p) const
    {
      // Combine the hashes of the two integers
      std::size_t h1 = std::hash<int32_t>{}(p.first);
      std::size_t h2 = std::hash<int32_t>{}(p.second);
      // Use a bitwise combination for mixing
      return h1 ^ (h2 << 1); // XOR and shift for a simple combination
    }
  };

protected:
  // Backend methods
  //! \brief Map ID spaces into indices for ordered access by downstream applications
  void map_id_spaces();

  //! Discover element faces on subdomain interfaces.
  void discover_surface_elements();

  //! Validate that explicit sideset faces are represented by discovered
  //! interface sets and record the corresponding interface associations.
  void map_sidesets_to_discovered_interfaces();

  //! Create surfaces for explicit sidesets, splitting them by discovered
  //! interface set when needed, and then create any remaining implicit
  //! interface surfaces that were not covered by sidesets.
  void create_surfaces_from_sidesets_and_interfaces();

  //! The "natural" orientation of triangle normals is dependent on the element
  //! used to defined the SidePair objects we use for unique identification of faces.
  //! This method ensures that the normals are consistent for each sideset and sets
  //! the senses of the parent volumes (mesh blocks) for each surface, updating
  //! curent SidePair objects to do so if necessary.
  void determine_surface_senses();

  void check_face_and_element_types() const;

  //! Build the one-to-many connectivity maps for the case where less refined elements share
  //! a face with several more refined elements.
  void build_amr_connectivity_map();

  //! Get the active, more refined, neighbors associated with a given side of an element.
  void get_active_neighbors(MeshID current_elem_id, int side, std::vector<std::pair<MeshID, int>> & refined_neighbors);

  // Attributes
  // a mesh managed by this class (if loading from a file)
  std::unique_ptr<libMesh::Mesh> managed_mesh_ {nullptr};
  // a pointer to the mesh this class is using for data-structure queries (may be managed externally)
  const libMesh::MeshBase* mesh_ {nullptr};

  struct CoarseToFineElemMap {
    int num_faces_ {-1};
    int max_face_neighbors_ {-1};
    // Vector of neighboring elements with the side pointing to this element
    std::vector<std::pair<MeshID, int>> neighboring_elems_;

    CoarseToFineElemMap() = default;

    std::pair<MeshID, int> refined_neighbor(int face, int neighbor_idx) const {
      return neighboring_elems_[face * max_face_neighbors_ + neighbor_idx];
    }

    int get_num_subfaces(int face) const {
      const int start = max_face_neighbors_ * face;
      const int end = max_face_neighbors_ * (face + 1);
      for (int i = start; i < end; ++i) {
        if (neighboring_elems_[i].first == LibMeshManager::INVALID_NEIGHBOR) {
          return i - start;
        }
      }
      return end - start;
    }
  };

  // mapping elements to their neighbors when a mesh is adaptive
  // key is an element that was found to have AMR connectivity
  std::unordered_map<MeshID, CoarseToFineElemMap> amr_conn_map_;

  // Ugh, double mapping
  std::unordered_map<MeshID, SidePair> mesh_id_to_sidepair_;
  std::unordered_map<SidePair, MeshID, SidePairHash> sidepair_to_mesh_id_;

  // sideset face mapping, stores the element and the side number
  // for each face in the mesh that lies on a boundary
  std::unordered_map<MeshID, std::vector<MeshID>> sideset_face_map_;

  //! Mapping of subdomain interfaces (Identified by subdomain ID pairs) to the
  //! set of element faces that make up the interface
  std::unordered_map<std::pair<MeshID, MeshID>, std::set<MeshID>, MeshIDPairHash>
  subdomain_interface_map_;

  //! Mapping of explicit sidesets to the discovered interface pairs that contain
  //! their valid faces. Same-block sideset faces are ignored.
  std::unordered_map<MeshID, std::set<std::pair<MeshID, MeshID>>> sideset_interface_map_;

  //! Mapping of explicit sidesets to the valid faces they contribute to each
  //! discovered interface pair.
  std::unordered_map<MeshID, std::map<std::pair<MeshID, MeshID>, std::vector<MeshID>>>
  sideset_interface_face_map_;

  //! Mapping of explicit sidesets to the created surfaces that should inherit
  //! their metadata.
  std::unordered_map<MeshID, std::vector<MeshID>> sideset_surface_map_;

  //! Mapping of surface IDs to the set of element faces that make up the surface,
  //! with the element face represented by assigned XDG IDs
  // TODO: store proper data types here
  std::unordered_map<MeshID, std::vector<MeshID>> surface_map_;

  //! Mapping of surfaces to the volumes on either side. Volumes are ordered
  //! based on their sense with respect to the surface triangles
  std::unordered_map<MeshID, std::pair<MeshID, MeshID>> surface_senses_;

  int32_t num_elements_ {-1};

  //! Mapping of surfaces to the volumes on either side. Volumes are ordered
  int32_t next_sidepair_id_ {1}; //!< Next available sidepair ID, starts at one
};

struct LibMeshElementFaceAccessor : public ElementFaceAccessor {

  LibMeshElementFaceAccessor(const LibMeshManager* mesh_manager, MeshID element) :
  ElementFaceAccessor(element), mesh_manager_(mesh_manager) {
    mesh_ = mesh_manager_->mesh();
    elem_ptr_ = mesh_->elem_ptr(element);
    // Early out of the hash-map lookup if the mesh doesn't have AMR connectivity.
    adaptive_ = mesh_manager_->has_amr_conn() ? mesh_manager_->is_elem_adaptive(element) : false;
  }

  std::vector<Vertex> face_vertices(int i) const override {
    std::vector<Vertex> coords;
    switch (elem_ptr_->type()) {
      case libMesh::TET4: {
        coords.reserve(3);
        for (int j = 0; j < 3; j++) {
          const auto node_ptr = elem_ptr_->node_ptr(libMesh::Tet4::side_nodes_map[i][j]);
          coords.push_back({(*node_ptr)(0), (*node_ptr)(1), (*node_ptr)(2)});
        }
        break;
      }
      case libMesh::HEX8: {
        coords.reserve(4);
        for (int j = 0; j < 4; j++) {
          const auto node_ptr = elem_ptr_->node_ptr(libMesh::Hex8::side_nodes_map[i][j]);
          coords.push_back({(*node_ptr)(0), (*node_ptr)(1), (*node_ptr)(2)});
        }
        break;
      }
      default:
        fatal_error("Unsupported element type {} in LibMeshElementFaceAccessor",
                    static_cast<int>(elem_ptr_->type()));
    }
    return coords;
  }

  int num_faces() const override {
    return static_cast<int>(elem_ptr_->n_sides());
  }

  bool has_sub_faces() const override { return adaptive_; }

  int num_sub_faces(int i) const override {
    return mesh_manager_->get_num_subfaces(element_, i);
  }

  // data members
  const LibMeshManager* mesh_manager_;
  const libMesh::MeshBase* mesh_;
  const libMesh::Elem* elem_ptr_;
  bool adaptive_;
};


} // namespace xdg

#endif // include guard
