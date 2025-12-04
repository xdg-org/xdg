#ifndef _XDG_LIBMESH_MESH_MANAGER
#define _XDG_LIBMESH_MESH_MANAGER

#include <memory>

#include "xdg/constants.h"
#include "xdg/element_face_accessor.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/error.h"

#include "libmesh/libmesh.h"
#include "libmesh/elem.h"
#include "libmesh/cell_tet4.h"
#include "libmesh/mesh.h"
namespace xdg {


class LibMeshManager : public MeshManager {

  constexpr static int SIDE_NONE {-1};

public:
  LibMeshManager(void* ptr);

  LibMeshManager();

  ~LibMeshManager() override = default;

  // Backend methods

  //! Discover element faces on sudbomain interfaces,
  //! create surfaces and topology accordingly,
  //! and assign transmission boundary conditions for these interfacdes.
  //! If pre-defined sidesets exist, they will replace discovered interfaces
  //! between subdomains. Full replacement of subdomain interfaces is required.
  void discover_surface_elements();

  //! If explicit sidesets exist, the elements in these sidesets should
  //! replace the elements in the discovered subdomain interfaces.
  void merge_sidesets_into_interfaces();

  //! For each user-defined sideset, create a surface and assign the associated
  //! elements to the surface. Also define new surfaces for any discovered interface
  //! sidesets if the sets are not empty after merging.
  void create_surfaces_from_sidesets_and_interfaces();

  //! The "natural" orientation of triangle normals is dependent on the element
  //! used to defined the SidePair objects we use for unique identification of faces.
  //! This method ensures that the normals are consistent for each sideset and sets
  //! the senses of the parent volumes (mesh blocks) for each surface, updating
  //! curent SidePair objects to do so if necessary.
  void determine_surface_senses();

  //! Create a new sideset for all faces on the boundary of the mesh.
  //! This is used for re-entrant particles if needed.
  void create_boundary_sideset();

  // Interface methods
  MeshLibrary mesh_library() const override { return MeshLibrary::LIBMESH; }

  void load_file(const std::string& filepath) override;

  void init() override;

  void parse_metadata() override;

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

  std::vector<MeshID> get_volume_elements(MeshID volume) const;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::array<Vertex, 3> face_vertices(MeshID triangle) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  std::vector<Vertex> get_surface_vertices(MeshID surface) const override
  {
    fatal_error("LibMeshManager::get_surface_vertices() not implemented yet");
  }

  std::pair<std::vector<Vertex>, std::vector<int>> get_surface_mesh(MeshID surface) const override
  {
    fatal_error("LibMeshManager::get_surface_mesh not implemented yet");
  }

  SurfaceElementType get_surface_element_type(MeshID surface) const override
  {
    fatal_error("LibMeshManager::get_surface_element_type() not implemented yet");
  }

  MeshID adjacent_element(MeshID element, int face) const override;

  double element_volume(MeshID element) const override;

    inline MeshID element_id(size_t element_idx) const override {
    // libMesh element IDs start at zero and , under normal circumstances,
    // are contiguous, so we can just return the index as the ID
    return element_idx;
  }

  inline int element_index(MeshID element) const override {
    // libMesh element IDs start at zero and , under normal circumstances,
    // are contiguous, so we can just return the ID as the index
    return element;
  }

  MeshID create_volume() override;

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override;

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  // Accessors
  const libMesh::Mesh* mesh() const { return mesh_.get(); }
  libMesh::Mesh* mesh() { return mesh_.get(); }

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
  std::unique_ptr<libMesh::Mesh> mesh_ {nullptr};

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
    tet_ = (const libMesh::Tet4*)elem_ptr_;
  }

  std::array<Vertex, 3> face_vertices(int i) const override {
    std::array<Vertex, 3> coords;
    for (int j = 0; j < 3; j++) {
      const auto node_ptr = elem_ptr_->node_ptr(tet_->side_nodes_map[i][j]);
      coords[j] = {(*node_ptr)(0), (*node_ptr)(1), (*node_ptr)(2)};
    }
    return std::move(coords);
  }

  // data members
  const LibMeshManager* mesh_manager_;
  const libMesh::MeshBase* mesh_;
  const libMesh::Tet4* tet_;
  const libMesh::Elem* elem_ptr_;
};


} // namespace xdg

#endif // include guard