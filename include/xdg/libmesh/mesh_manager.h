#ifndef _XDG_LIBMESH_MESH_MANAGER
#define _XDG_LIBMESH_MESH_MANAGER

#include <map>
#include <memory>

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

public:
  LibMeshManager(const libMesh::MeshBase* ptr);

  LibMeshManager();

  ~LibMeshManager() override = default;

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

  // Interface methods
  MeshLibrary mesh_library() const override { return MeshLibrary::LIBMESH; }

  void load_file(const std::string& filepath) override;

  void init() override;

  void parse_metadata() override;

  void check_face_and_element_types() const;

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

  VolumeElementType get_volume_element_type(MeshID volume) const override;

  MeshID adjacent_element(MeshID element, int face) const override;

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
      side.first = old_side.first;
      side_num_ = old_side.second;
      side.second = old_side.first->neighbor_ptr(side_num_);
      if (side.second != nullptr) {
        set_order();
        set_side_num();
      }
    }

    SidePair(const libMesh::Elem* elem, int side_num) {
      side.first = elem;
      side_num_ = side_num;
      side.second = elem->neighbor_ptr(side_num);
      if (side.second != nullptr) {
        set_order();
        set_side_num();
      }
    }

    SidePair(const libMesh::Elem* elem1, const libMesh::Elem* elem2) {
      side.first = elem1;
      side.second = elem2;
      if (elem1 == nullptr || elem2 == nullptr) {
        fatal_error("SidePair pointer constructor must be used with non-null pointers");
      }
      set_order();
      set_side_num();
    }

    SidePair(std::pair<const libMesh::Elem*, const libMesh::Elem*> side_pair) : side(side_pair) {
      if (side.first == nullptr || side.second == nullptr) {
        fatal_error("SidePair pair constructor must be used with non-null pointers");
      }
      set_order();
      set_side_num();
    }

    void set_order() {
      if (side.first == nullptr && side.second == nullptr) {
        fatal_error("SidePair created with null elements");
      }

      // re-order so that null pointers come second if present
      if (side.first == nullptr) {
        std::swap(side.first, side.second);
        return;
      }

      // if a null pointer is present, the remaining checks aren't necessary
      if (side.second == nullptr) {
        return;
      }

      if (side.first->id() == side.second->id()) {
        fatal_error("SidePair created with the same element on both sides. Element ID: {}", side.first->id());
      }

      if (side.first->id() > side.second->id()) {
        std::swap(side.first, side.second);
      }
    }

    SurfaceFaceType face_type() const {
      switch (side.first->type()) {
        case libMesh::TET4:
          return SurfaceFaceType::TRI;
        case libMesh::HEX8:
          return SurfaceFaceType::QUAD;
        default:
          return SurfaceFaceType::UNSUPPORTED;
      }
    }

    //! Retrieve a vertex of the side pair face, using the type
    //! to access the node reference though an internal, non-blocking
    //! data structure
    template<typename T>
    Vertex vertex(int i) const {
      const auto e_type = static_cast<const T*>(first());
      const auto& node_ref = first()->node_ref(e_type->side_nodes_map[side_num()][i]);
      return {node_ref(0), node_ref(1), node_ref(2)};
    }

    void set_side_num() {
      for (int i = 0; i < side.first->n_sides(); i++) {
        if (side.first->neighbor_ptr(i) == side.second) {
          side_num_ = i;
          break;
        }
      }
      if (side_num_ == SIDE_NONE) {
        fatal_error("SidePair created with elements that are not neighbors");
      }
    }

    void swap() {
      std::swap(side.first, side.second);
      set_side_num();
    }

    bool operator==(const SidePair& other) const
    {
      return first() == other.first() && side_num() == other.side_num();
    }

    bool operator<(const SidePair& other) const
    {
      return side < other.side;
    }

    const libMesh::Elem* first() const { return side.first; }
    const libMesh::Elem* second() const { return side.second; }

    const int32_t side_num() const { return side_num_; }

    const std::unique_ptr<const libMesh::Elem> face_ptr() const {

      return first()->side_ptr(side_num());
    }

    std::pair<const libMesh::Elem*, const libMesh::Elem*> side {nullptr, nullptr};
    int32_t side_num_ {SIDE_NONE};
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

  // Attributes
  protected:
  // a mesh managed by this class (if loading from a file)
  std::unique_ptr<libMesh::Mesh> managed_mesh_ {nullptr};
  // a pointer to the mesh this class is using for data-structure queries (may be managed externally)
  const libMesh::MeshBase* mesh_ {nullptr};

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

  // data members
  const LibMeshManager* mesh_manager_;
  const libMesh::MeshBase* mesh_;
  const libMesh::Elem* elem_ptr_;
};


} // namespace xdg

#endif // include guard
