#ifndef _XDG_MOAB_MESH_MANAGER
#define _XDG_MOAB_MESH_MANAGER

#include <vector>
#include <unordered_map>

#include "xdg/mesh_manager_interface.h"
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
  MOABMeshManager(moab::Interface* mbi);

  MOABMeshManager();

  // Methods
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

  int num_volume_faces(MeshID volume) const override;

  int num_surface_faces(MeshID surface) const override;

  std::vector<MeshID> get_volume_elements(MeshID volume) const override;

  std::vector<MeshID> get_surface_faces(MeshID surface) const override;

  std::vector<Vertex> element_vertices(MeshID element) const override;

  std::array<Vertex, 3> face_vertices(MeshID element) const override;

  std::vector<int> get_surface_connectivity(MeshID surface) const;

  std::vector<double> get_surface_vertices(MeshID surface) const;

  // Topology
  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

  Sense surface_sense(MeshID surface, MeshID volume) const override;

  // Metadata
  void parse_metadata() override;

  void graveyard_check();

  // Other
  MeshLibrary mesh_library() const override { return MeshLibrary::MOAB; }

private:
  // Internal MOAB methods
  std::vector<moab::EntityHandle> _ents_of_dim(int dim) const;
  moab::Range _surface_faces(MeshID surface) const;

  std::string get_volume_property(const std::string& property, MeshID vol) const;

public:
  // Accessors
  moab::Interface* moab_interface() const { return moab_raw_ptr_; };
  const auto& mb_direct() const { return mdam_; }
  moab::EntityHandle root_set() const { return 0; }

private:
  std::shared_ptr<moab::Interface> shared_moab_instance_;
  moab::Interface* moab_raw_ptr_;
  std::shared_ptr<MBDirectAccess> mdam_;

  // Maps from XDG identifiers to MOAB handles
  std::unordered_map<MeshID, moab::EntityHandle> volume_id_map_;
  std::unordered_map<MeshID, moab::EntityHandle> surface_id_map_;

  // tag handles
  moab::Tag geometry_dimension_tag_;
  moab::Tag global_id_tag_;
  moab::Tag category_tag_;
  moab::Tag name_tag_;
  moab::Tag surf_to_volume_sense_tag_;

  // TODO: Make this comprehensive or a parameter in the constructor
  inline static const std::string metadata_delimiters = ":";

};

} // namespace xdg

#endif

