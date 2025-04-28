
// Mock data for mesh interface testing

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/mesh_manager_interface.h"

using namespace xdg;

class MeshMock : public MeshManager {
public:
  MeshMock() {
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
    return 0;
  }

  virtual int num_volume_faces(MeshID volume) const override {
    return 12;
  }

  virtual int num_surface_faces(MeshID surface) const override {
    return 2;
  }

  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const override {
    return {0};
  }

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const override {
    int start = surface * 2;
    return {start, start + 1};
  }

  virtual std::vector<Vertex> element_vertices(MeshID element) const override {
    const auto& conn = triangle_connectivity[element];
    return {vertices[conn[0]], vertices[conn[1]], vertices[conn[2]]};
  }

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override {
    const auto vertices = element_vertices(element);
    return {vertices[0], vertices[1], vertices[2]};
  }

  std::vector<int> get_surface_connectivity(MeshID surface) const override
  {
    std::vector<int> flat_connectivity;
    flat_connectivity.reserve(triangle_connectivity.size() * 3);
    
    for (const auto& tri : triangle_connectivity) {
      flat_connectivity.insert(flat_connectivity.end(), tri.begin(), tri.end());
    }

    return flat_connectivity;
  }

  std::vector<Vertex> get_surface_vertices(MeshID surface) const override
  {
    fatal_error("MockMesh does not support get_surface_vertices()");

  }

  // Topology
  virtual std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override {
    return {0, ID_NONE};
  }

  virtual std::vector<MeshID> get_volume_surfaces(MeshID volume) const override {
    return {0, 1, 2, 3, 4, 5};
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

  virtual SurfaceElementType get_surface_element_type(MeshID surface) const override {
    fatal_error("MockMesh does not support get_surface_element_type()");
  }

  // Other
  virtual MeshLibrary mesh_library() const override { return MeshLibrary::INTERNAL; }

// Data members
private:
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
    {bounding_box.min_x, bounding_box.min_y, bounding_box.min_z}
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

};