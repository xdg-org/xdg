
#ifndef _XDG_MESH_MANAGER_INTERFACE
#define _XDG_MESH_MANAGER_INTERFACE

#include <string>
#include <vector>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/vec3da.h"

namespace xdg {

class MeshManager {
public:

  // Abstract Methods

  // Setup
  virtual void load_file(const std::string& filepath) = 0;

  virtual void init() = 0;

  // Geometry
  virtual int num_volumes() const = 0;

  virtual int num_surfaces() const = 0;

  virtual int num_ents_of_dimension(int dim) const = 0;

  // Mesh
  virtual int num_volume_elements(MeshID volume) const = 0;

  virtual int num_volume_faces(MeshID volume) const = 0;

  virtual int num_surface_faces(MeshID surface) const = 0;

  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const = 0;

  std::vector<MeshID> get_volume_faces(MeshID volume) const;

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const = 0;

  // TODO: can we accomplish this without allocating memory?
  virtual std::vector<Vertex> element_vertices(MeshID element) const = 0;

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const = 0;

  virtual std::vector<int> get_surface_connectivity(MeshID surface) const = 0;

  virtual std::vector<double> get_surface_vertices(MeshID surface) const = 0;


  BoundingBox element_bounding_box(MeshID element) const;

  BoundingBox face_bounding_box(MeshID element) const;

  BoundingBox volume_bounding_box(MeshID volume) const;

  BoundingBox surface_bounding_box(MeshID surface) const;

  Direction face_normal(MeshID element) const;

  // Topology
  // Returns parent with forward sense, then reverse
  std::pair<MeshID, MeshID> get_parent_volumes(MeshID surface) const;

  virtual std::vector<MeshID> get_volume_surfaces(MeshID volume) const = 0;

  virtual std::pair<MeshID, MeshID> surface_senses(MeshID surface) const = 0;

  virtual Sense surface_sense(MeshID surface, MeshID volume) const = 0;

  virtual MeshID create_volume() = 0;

  virtual void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) = 0;

  MeshID next_volume(MeshID current_volume, MeshID surface) const;

  // Methods
  MeshID next_volume_id() const;

  MeshID next_surface_id() const;

  MeshID create_implicit_complement();

  // Metadata methods
  virtual void parse_metadata() = 0;

  bool volume_has_property(MeshID volume, PropertyType type) const;
  bool surface_has_property(MeshID surface, PropertyType type) const;

  Property get_volume_property(MeshID volume, PropertyType type) const;
  Property get_surface_property(MeshID surface, PropertyType type) const;

  // Accessors
  const std::vector<MeshID>& volumes() const { return volumes_; }
  std::vector<MeshID>& volumes() { return volumes_; }
  const std::vector<MeshID>& surfaces() const { return surfaces_; }
  std::vector<MeshID>& surfaces() { return surfaces_; }
  MeshID implicit_complement() const { return implicit_complement_; }

  virtual MeshLibrary mesh_library() const = 0;

protected:
  // metadata
  std::map<std::pair<MeshID, PropertyType>, Property> volume_metadata_;
  std::map<std::pair<MeshID, PropertyType>, Property> surface_metadata_;

  std::vector<MeshID> volumes_;
  std::vector<MeshID> surfaces_;

  // TODO: attempt to remove this attribute
  MeshID implicit_complement_ {ID_NONE};

};

} // namespace xdg

#endif