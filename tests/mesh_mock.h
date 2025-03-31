
// Mock data for mesh interface testing

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/mesh_manager_interface.h"

using namespace xdg;

class MeshMock : public MeshManager {
public:
  MeshMock(bool volumetric_elements = true) : volumetric_elements_(volumetric_elements) {
    volumes_ = {0};
    surfaces_ = {0, 1, 2, 3, 4, 5};
  }

  void load_file(const std::string& file_name) override {}

  void init() override {}

  virtual int num_volumes() const override {
    return 1;
  }

  virtual int num_surfaces() const override {
    return 6;
  }

  virtual int num_ents_of_dimension(int dim) const override {
   switch (dim)
   {
   case 2:
    return 6;
    break;
   case 3:
    return 1;
    break;
   default:
    fatal_error("MockMesh does not support num_ents_of_dimension() for dimension {}", dim);
    break;
   }
    return -1;
  }

  virtual int num_volume_elements(MeshID volume) const override {
    if (!volumetric_elements_) return 0;
    return 12;
  }

  virtual int num_volume_faces(MeshID volume) const override {
    return 12;
  }

  virtual int num_surface_faces(MeshID surface) const override {
    return 2;
  }

  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const override {
    if (!volumetric_elements_) return {};
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // returning all tetrahedron elements
  }

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const override {
    int start = surface * 2;
    return {start, start + 1};
  }

  virtual std::vector<Vertex> element_vertices(MeshID element) const override {
    if (!volumetric_elements_) return {};
    const auto& conn = tetrahedron_connectivity[element];
    return {vertices[conn[0]], vertices[conn[1]], vertices[conn[2]], vertices[conn[3]]};
  }

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override {
    const auto& conn = triangle_connectivity[element];
    return {vertices[conn[0]], vertices[conn[1]], vertices[conn[2]]};
  }

  // Topology
  virtual std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override {
    return {0, ID_NONE};
  }

  virtual std::vector<MeshID> get_volume_surfaces(MeshID volume) const override {
    return {0, 1, 2, 3, 4, 5};
  }

  std::vector<std::pair<MeshID, double>>
  walk_elements(MeshID starting_element,
                const Position& start,
                const Position& end) const override
  {
    return {};
  }

  Sense surface_sense(MeshID surface, MeshID volume) const override {
    return Sense::FORWARD;
  }

  virtual MeshID create_volume() override {
    fatal_error("MockMesh does not support create_volume()");
    return ID_NONE;
  }

  virtual void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override {
    fatal_error("MockMesh does not support add_surface_to_volume()");
  }

  virtual void parse_metadata() override {
    fatal_error("MockMesh does not support parse_metadata()");
  }

  // Other
  virtual MeshLibrary mesh_library() const override { return MeshLibrary::INTERNAL; }

// Data members
private:
  bool volumetric_elements_; // flag to indicate if the mesh has volumetric elements

  const BoundingBox bounding_box {-2.0, -3.0, -4.0, 5.0, 6.0, 7.0};

  const std::vector<Position> vertices {
    // vertices in the upper z plane
    {bounding_box.max_x, bounding_box.min_y, bounding_box.max_z},
    {bounding_box.max_x, bounding_box.max_y, bounding_box.max_z},
    {bounding_box.min_x, bounding_box.max_y, bounding_box.max_z},
    {bounding_box.min_x, bounding_box.min_y, bounding_box.max_z},
    // vertices in the lower z plane
    {bounding_box.max_x, bounding_box.min_y, bounding_box.min_z},
    {bounding_box.max_x, bounding_box.max_y, bounding_box.min_z},
    {bounding_box.min_x, bounding_box.max_y, bounding_box.min_z},
    {bounding_box.min_x, bounding_box.min_y, bounding_box.min_z},
    {bounding_box.center()} // bounding box center for tet elements
  };

  const std::vector<std::array<int, 3>>  triangle_connectivity {
  // lower z face
  {0, 1, 3},
  {3, 1, 2},
  // upper z face
  {4, 7, 5},
  {7, 6, 5},
  // lower x face
  {6, 3, 2},
  {7, 3, 6},
  // upper x face
  {0, 4, 1},
  {5, 1, 4},
  // lower y face
  {0, 3, 4},
  {7, 4, 3},
  // upper y face
  {1, 6, 2},
  {6, 1, 5}
  };

  // tetrahedron connectivity for a cube, all elements connect to the center vertex
  // there are 12 tetrahedron in total
  const std::vector<std::array<int, 4>> tetrahedron_connectivity {
    // each tetrahedron is defined by 4 vertices, the last vertex is the center of the bounding box
    {0, 1, 2, 8}, // lower z plane
    {0, 2, 3, 8},
    {4, 5, 6, 8}, // upper z plane
    {4, 6, 7, 8},
    {0, 1, 5, 8}, // lower x plane
    {0, 5, 4, 8},
    {2, 3, 7, 8}, // upper x plane
    {2, 7, 6, 8},
    {0, 3, 7, 8}, // lower y plane
    {0, 7, 4, 8},
    {1, 2, 6, 8}, // upper y plane
    {1, 6, 5, 8}
   };
};