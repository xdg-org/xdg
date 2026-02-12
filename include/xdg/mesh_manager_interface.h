
#ifndef _XDG_MESH_MANAGER_INTERFACE
#define _XDG_MESH_MANAGER_INTERFACE

#include <string>
#include <vector>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/id_block_map.h"
#include "xdg/vec3da.h"

namespace xdg {

class MeshManager {
public:

  MeshManager();

  // Abstract Methods
  virtual ~MeshManager() = default;

  // Setup
  virtual void load_file(const std::string& filepath) = 0;

  virtual void init() = 0;

  // Geometry
  virtual int num_volumes() const = 0;

  virtual int num_surfaces() const = 0;

  virtual int num_ents_of_dimension(int dim) const = 0;

  //! \brief Walk through elements along a ray from start to end position.
  //! \note It is assumed that the provided position is within the starting element.
  //! \param starting_element The initial element to start the walk from
  //! \param start The starting position of the ray
  //! \param end The ending position of the ray
  //! \return Vector of pairs containing element IDs and distances traveled through each element
  std::vector<std::pair<MeshID, double>>
  walk_elements(MeshID starting_element,
                const Position& start,
                const Position& end) const;

  //! \brief Walk through elements along a ray with specified direction and distance
  //! \note It is assumed that the provided position is within the starting element.
  //! \param starting_element The initial element to start the walk from
  //! \param start The starting position of the ray
  //! \param u The normalized direction vector of the ray
  //! \param distance The total distance to travel along the ray
  //! \return Vector of pairs containing element IDs and distances traveled through each element
  std::vector<std::pair<MeshID, double>>
  walk_elements(MeshID starting_element,
                const Position& start,
                const Direction& u,
                double distance) const;

  //! \brief Find the next element along a ray from the current position.
  //! \note It is assumed that the provided position is within the element.
  //! \param current_element The current element being traversed
  //! \param r The current position within the element
  //! \param u The normalized direction vector of the ray
  //! \return Pair containing the next element ID and distance to the exit point
  std::pair<MeshID, double>
  next_element(MeshID current_element,
               const Position& r,
               const Position& u) const;

  // Mesh
  virtual int num_vertices() const = 0;

  virtual int num_volume_elements(MeshID volume) const = 0;

  virtual int num_volume_elements() const;

  virtual int num_volume_faces(MeshID volume) const = 0;

  virtual int num_surface_faces(MeshID surface) const = 0;

  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const = 0;

  std::vector<MeshID> get_volume_faces(MeshID volume) const;

  // Return an array of connectivity indices for a given volume in the model
  virtual std::vector<int> get_volume_connectivity(MeshID volume) const = 0;

  virtual std::vector<Vertex> get_volume_vertices(MeshID volume) const = 0;

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const = 0;
  // TODO: can we accomplish this without allocating memory?
  virtual std::vector<Vertex> element_vertices(MeshID element) const = 0;

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const = 0;

  virtual std::vector<Vertex> get_surface_vertices(MeshID surface) const = 0;

  //! \brief Return a vertex ID given its index in the mesh
  virtual MeshID vertex_id(size_t vertex_idx) const
  { return vertex_id_map_.index_to_id(vertex_idx); }

  //! \brief Return the index of a vertex given its ID in the mesh
  virtual MeshIndex vertex_index(MeshID vertex) const
  { return vertex_id_map_.id_to_index(vertex); }

  // Return an array of connectivity indices for a given surface in the model
  virtual std::vector<int> get_surface_connectivity(MeshID surface) const = 0;

  virtual SurfaceElementType get_surface_element_type(MeshID element) const = 0;

  //! \brief Convert an element's ID to its index in the mesh
  //! \param element_idx The index of the element in the mesh
  //! \return The element ID
  virtual MeshID element_id(size_t element_idx) const
  { return volume_element_id_map_.index_to_id(element_idx); }

  //! \brief Convert an element's index in the mesh to its ID
  //! \param element The element ID
  //! \return The index of the element in the mesh
  virtual MeshIndex element_index(MeshID element) const
  { return volume_element_id_map_.id_to_index(element); }

  //! \brief Get the adjacent element across a given face
  //! \param element The current element ID
  //! \param face The local face index (0-3 for tetrahedra)
  //! \return The ID of the adjacent element, or ID_NONE if the face is on a boundary
  virtual MeshID adjacent_element(MeshID element, int face) const = 0;

  //! \brief Get the volume of a given element
  //! \param element The element ID
  //! \return The volume of the element
  virtual double element_volume(MeshID element) const = 0;

  //! \brief Get the coordinates of a vertex by its ID
  //! \param vertex_id The vertex ID
  //! \return The coordinates of the vertex as an xdg::Vertex
  virtual
  Vertex vertex_coordinates(MeshID vertex_id) const = 0;

  //! \brief Get the connectivity of an element by its ID
  //! \param element The element ID
  //! \return A vector of vertex IDs that make up the element
  virtual
  std::vector<MeshID> element_connectivity(MeshID element) const = 0;

  BoundingBox element_bounding_box(MeshID element) const;

  BoundingBox face_bounding_box(MeshID element) const;

  BoundingBox surface_bounding_box(MeshID surface) const;

  BoundingBox volume_bounding_box(MeshID volume) const;

  BoundingBox global_bounding_box() const;

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

  //! Block ID mapping from element IDs to contiguous index space
  IDBlockMapping<MeshID> volume_element_id_map_;

  //! Block ID mapping from vertex IDs to contiguous index space
  IDBlockMapping<MeshID> vertex_id_map_;

  // TODO: attempt to remove this attribute
  MeshID implicit_complement_ {ID_NONE};
};

} // namespace xdg

#endif
