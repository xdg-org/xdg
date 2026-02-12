#ifndef _XDG_MOAB_MESH_MANAGER
#define _XDG_MOAB_MESH_MANAGER

#include <vector>
#include <unordered_map>

#include "xdg/mesh_manager_interface.h"
#include "xdg/element_face_accessor.h"
#include "xdg/moab/direct_access.h"
#include "xdg/moab/metadata.h"

#include "moab/Core.hpp"
#include "moab/Types.hpp"

namespace xdg {

static const char VOLUME_CATEGORY_VALUE[32] = "Volume";
static const char SURFACE_CATEGORY_VALUE[32] = "Surface";
static const char GROUP_CATEGORY_VALUE[32] = "Group";

static const std::map<std::string, PropertyType> MOAB_PROPERTY_MAP
{
  {"mat", PropertyType::MATERIAL},
  {"material", PropertyType::MATERIAL},
  {"boundary", PropertyType::BOUNDARY_CONDITION},
  {"temp", PropertyType::TEMPERATURE}
};


class MOABMeshManager : public MeshManager {

public:
  // Constructors
  MOABMeshManager(moab::Interface* mbi);

  MOABMeshManager();

  // Interface Methods
  MeshLibrary mesh_library() const override { return MeshLibrary::MOAB; }

  void load_file(const std::string& filepath);

  void init() override;

  // Geometry
  int num_volumes() const override;

  int num_surfaces() const override;

  int num_ents_of_dimension(int dim) const override;

  MeshID create_volume() override;

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override;

  // Mesh
  int num_volume_elements(MeshID volume) const override;

  int num_volume_elements() const override;

  int num_volume_faces(MeshID volume) const override;

  int num_surface_faces(MeshID surface) const override;

  int num_vertices() const override;

  std::vector<MeshID> get_volume_elements(MeshID volume) const override;

  // Map from global volume connectivity indices to local volume connectivity indices and return the local connectivity for a volume
  std::vector<int> get_volume_connectivity(MeshID volume) const override;

  std::vector<Vertex> get_volume_vertices(MeshID volume) const override;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<MeshID> element_connectivity(MeshID element) const override;

  Vertex vertex_coordinates(MeshID vertex) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::array<Vertex, 3> face_vertices(MeshID element) const override;

  // Map from global surface connectivity indices to local surface connectivity indices and return the local connectivity for a surface
  std::vector<int> get_surface_connectivity(MeshID surface) const override;

  std::vector<Vertex> get_surface_vertices(MeshID surface) const override;

  SurfaceElementType get_surface_element_type(MeshID surface) const override;

  MeshID adjacent_element(MeshID element, int face) const override;

  double element_volume(MeshID element) const override;

  // Topology
  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  // Metadata
  void parse_metadata() override;

private:
  // Internal MOAB methods

  /**
   * @brief Sets up required MOAB tags for geometry management
   *
   * Ensures all necessary MOAB tag handles exist and are properly initialized.
   * If any tag does not exist, it will be created with appropriate settings.
   * Fatal errors are thrown if tag creation/retrieval fails.
   */
  void setup_tags();

  /**
   * @brief Checks for volumes with graveyard material and sets vacuum boundary conditions.
   *
   * This method iterates through all volumes in the mesh and checks if any have a material
   * property set to "graveyard" (case insensitive). For any volume using a graveyard material,
   * all of its surfaces will have their boundary condition property set to "vacuum".
   *
   * This ensures proper handling of graveyard regions which are typically used to define
   * problem boundaries where particles should be terminated.
   */
  void graveyard_check();

  /**
   * @brief Creates and registers a new surface that consists of the exterior faces of the mesh.
   *
   * This method identifies all exterior (boundary) faces of the mesh, creates a new surface entity
   * to represent them, and registers this surface within the mesh manager. The returned MeshID
   * corresponds to the newly created boundary surface. Note: this method assumes that the boundary
   * faces all have outward-facing normals with respect to the mesh.
   *
   * @return MeshID The identifier of the created boundary surface.
   */
  MeshID create_boundary_surface();

  std::vector<moab::EntityHandle> _ents_of_dim(int dim) const;
  moab::Range _surface_faces(MeshID surface) const;
  moab::Range _volume_elements(MeshID volume) const;
  std::vector<Vertex> _get_coords(moab::Range& verts) const;
  std::string get_volume_property(const std::string& property, MeshID vol) const;

public:
  // Accessors
  moab::Interface* moab_interface() const { return moab_raw_ptr_; };
  const std::shared_ptr<MBDirectAccess>& mb_direct() const { return mdam_; }
  moab::EntityHandle root_set() const { return 0; }

protected:

  template<typename T>
  std::vector<T> tag_data(moab::Tag tag, const moab::Range& entities) const {
    if (entities.empty()) {
      return std::vector<T>();
    }
    // ensure data container is sized appropriately
    std::vector<T> data(entities.size());
    this->moab_interface()->tag_get_data(tag, entities, data.data());
    return data;
  }

  template<typename T>
  std::vector<T> tag_data(moab::Tag tag, const std::vector<moab::EntityHandle>& entities, int length=1) const {
    if (entities.empty()) {
      return std::vector<T>();
    }
    std::vector<T> data(entities.size() * length);
    this->moab_interface()->tag_get_data(tag, entities.data(), entities.size(), data.data());
    return data;
  }

  template<typename T>
  std::vector<T> tag_data(moab::Tag tag, const moab::EntityHandle entity, int length) const {
    std::vector<T> data(length);
    this->moab_interface()->tag_get_data(tag, &entity, 1, data.data());
    return data;
  }

  template<typename T>
  T tag_data(moab::Tag tag, const moab::EntityHandle entity) const {
    T data;
    this->moab_interface()->tag_get_data(tag, &entity, 1, &data);
    return data;
  }

  std::string tag_data(moab::Tag tag, const moab::EntityHandle entity, int length) const {
    std::string data(' ', length);
    this->moab_interface()->tag_get_data(tag, &entity, 1, data.data());
    return data;
  }

private:
  std::shared_ptr<moab::Interface> shared_moab_instance_;
  moab::Interface* moab_raw_ptr_;
  std::shared_ptr<MBDirectAccess> mdam_;

  // Maps from XDG identifiers to MOAB handles
  std::unordered_map<MeshID, moab::EntityHandle> volume_id_map_;
  std::unordered_map<MeshID, moab::EntityHandle> surface_id_map_;

  // Maps elements to their volume ID
  std::unordered_map<MeshID, MeshID> element_volume_ids_;

  // tag handles
  moab::Tag geometry_dimension_tag_;
  moab::Tag global_id_tag_;
  moab::Tag category_tag_;
  moab::Tag name_tag_;
  moab::Tag surf_to_volume_sense_tag_;

  // TODO: Make this comprehensive or a parameter in the constructor
  inline static const std::string metadata_delimiters = ":";
};

struct MOABElementFaceAccessor : public ElementFaceAccessor {

  MOABElementFaceAccessor(const MOABMeshManager* mesh_manager, MeshID element) :
  ElementFaceAccessor(element), mesh_manager_(mesh_manager), element_ordering_(mesh_manager->mb_direct()->get_face_ordering(moab::MBTET)) {

    auto moab_mesh_manager = dynamic_cast<const MOABMeshManager*>(mesh_manager);
    if (!moab_mesh_manager) {
      throw std::runtime_error("MOABElementFaceAccessor requires a MOABMeshManager");
    }
    mesh_manager_ = moab_mesh_manager;
    element_coordinates_ = mesh_manager_->element_vertices(element);
  }

  std::array<Vertex, 3> face_vertices(int i) const override {
    std::array<Vertex, 3> verts;
    for (int j = 0; j < 3; j++) {
      verts[j] = element_coordinates_[element_ordering_[i][j]];
    }
    return std::move(verts);
  }

  // data members
  const MOABMeshManager* mesh_manager_;
  std::vector<Vertex> element_coordinates_;
  const std::vector<std::vector<int>>& element_ordering_;
};

} // namespace xdg

#endif
