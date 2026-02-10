// Mock data for mesh interface testing
#include <algorithm>
#include <unordered_map>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/element_face_accessor.h"

#include "xdg/geometry/measure.h"
#include "xdg/geometry/plucker.h"

using namespace xdg;

class MeshMock : public MeshManager {
public:
  MeshMock(bool volumetric_elements = true) : volumetric_elements_(volumetric_elements) {
    volumes_ = {0};
    surfaces_ = {0, 1, 2, 3, 4, 5};

    // Initialize some internal topology data structures
    volume_surfaces_map_[0] = surfaces_;
    for (int surface : surfaces_) {
      surface_sense_map_[surface] = {0, ID_NONE};
    }
  }

  // Required overloads
  void load_file(const std::string& file_name) override {}
  void init() override {}

  // Counts
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

  virtual int num_vertices() const override {
    return vertices_.size();
  }

  // Lists
  virtual std::vector<MeshID> get_volume_elements(MeshID volume) const override {
    if (!volumetric_elements_) return {};
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // returning all tetrahedron elements
  }

  virtual std::vector<MeshID> get_surface_faces(MeshID surface) const override {
    int start = surface * 2;
    return {start, start + 1};
  }

  // Coordinates
  virtual std::vector<Vertex> element_vertices(MeshID element) const override {
    if (!volumetric_elements_) return {};
    const auto& conn = tetrahedron_connectivity()[element];
    return {vertices()[conn[0]], vertices()[conn[1]], vertices()[conn[2]], vertices()[conn[3]]};
  }

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override {
    const auto& conn = triangle_connectivity()[element];
    return {vertices()[conn[0]], vertices()[conn[1]], vertices()[conn[2]]};
  }

  std::vector<Vertex> get_surface_vertices(MeshID surface) const override
  {
    // Get the faces for the given surface
    auto faces = get_surface_faces(surface);

    // Collect all unique vertices for the surface in encounter order
    std::unordered_map<int, int> handle_to_index;
    std::vector<Vertex> vertices;
    int local_index = 0;

    for (const auto& face : faces) {
      const auto& conn = triangle_connectivity()[face];
      for (const auto& global_index : conn) {
        if (handle_to_index.find(global_index) == handle_to_index.end()) {
          handle_to_index[global_index] = local_index++;
          vertices.push_back(this->vertices()[global_index]);
        }
      }
    }

    return vertices;
  }

  std::vector<int> get_surface_connectivity(MeshID surface) const override
  {
    // Get the faces for the given surface
    auto faces = get_surface_faces(surface);

    // Collect all unique vertices for the surface
    std::unordered_map<int, int> handle_to_index;
    int local_index = 0;

    for (const auto& face : faces) {
      const auto& conn = triangle_connectivity()[face];
      for (const auto& global_index : conn) {
        if (handle_to_index.find(global_index) == handle_to_index.end()) {
          handle_to_index[global_index] = local_index++;
        }
      }
    }

    // Build the connectivity array using local indices
    std::vector<int> connectivity;
    connectivity.reserve(faces.size() * 3);
    for (const auto& face : faces) {
      const auto& conn = triangle_connectivity()[face];
      for (const auto& global_index : conn) {
        connectivity.push_back(handle_to_index[global_index]);
      }
    }

    return connectivity;
  }

  std::vector<Vertex> get_volume_vertices(MeshID volume) const override
  {
    if (!volumetric_elements_) return {};

    auto elements = get_volume_elements(volume);

    std::unordered_map<int, int> handle_to_index;
    std::vector<Vertex> vertices;
    int local_index = 0;

    for (const auto& element : elements) {
      const auto& conn = tetrahedron_connectivity()[element];
      for (const auto& global_index : conn) {
        if (handle_to_index.find(global_index) == handle_to_index.end()) {
          handle_to_index[global_index] = local_index++;
          vertices.push_back(this->vertices()[global_index]);
        }
      }
    }

    return vertices;
  }

  std::vector<int> get_volume_connectivity(MeshID volume) const override
  {
    if (!volumetric_elements_) return {};

    auto elements = get_volume_elements(volume);

    std::unordered_map<int, int> handle_to_index;
    int local_index = 0;

    for (const auto& element : elements) {
      const auto& conn = tetrahedron_connectivity()[element];
      for (const auto& global_index : conn) {
        if (handle_to_index.find(global_index) == handle_to_index.end()) {
          handle_to_index[global_index] = local_index++;
        }
      }
    }

    std::vector<int> connectivity;
    connectivity.reserve(elements.size() * 4);
    for (const auto& element : elements) {
      const auto& conn = tetrahedron_connectivity()[element];
      for (const auto& global_index : conn) {
        connectivity.push_back(handle_to_index[global_index]);
      }
    }

    return connectivity;
  }

  // Topology
  virtual std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override {
    return surface_sense_map_.at(surface);
  }

  virtual std::vector<MeshID> get_volume_surfaces(MeshID volume) const override {
    return {0, 1, 2, 3, 4, 5};
  }

  std::array<std::array<int, 3>, 4> tet_faces(const std::array<int, 4>& tet) const {
    return {{
      {tet[0], tet[1], tet[2]},
      {tet[0], tet[2], tet[3]},
      {tet[0], tet[3], tet[1]},
      {tet[1], tet[3], tet[2]}
    }};
  }

  Sense surface_sense(MeshID surface, MeshID volume) const override {
    auto it = surface_sense_map_.find(surface);
    if (it == surface_sense_map_.end()) {
      fatal_error(fmt::format("Surface {} not found in surface_sense_map_", surface));
    }
    auto sense_pair = it->second;
    if (sense_pair.first == volume) {
      return Sense::FORWARD;
    } else if (sense_pair.second == volume) {
      return Sense::REVERSE;
    }
    fatal_error(fmt::format("Volume {} not found in surface_sense_map_ for surface {}", volume, surface));
    return Sense::UNSET;
  }

  virtual MeshID create_volume() override {
    volumes_.push_back(volumes_.size());
    return volumes_.back();
  }

  virtual void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override {
    auto& volume_surfaces = volume_surfaces_map_[volume];
    if (!overwrite && std::find(volume_surfaces.begin(), volume_surfaces.end(), surface) != volume_surfaces.end()) {
      fatal_error(fmt::format("Surface {} already exists in volume {}", surface, volume));
    }
    volume_surfaces.push_back(surface);
    if (sense == Sense::FORWARD) {
      surface_sense_map_[surface].first = volume;
    } else if (sense == Sense::REVERSE) {
      surface_sense_map_[surface].second = volume;
    }
  }

  virtual void parse_metadata() override {
    fatal_error("MockMesh does not support parse_metadata()");
  }

  BoundingBox bounding_box() const {
    return bounding_box_;
  }

  const std::vector<Vertex>& vertices() const {
    return vertices_;
  }

  const std::vector<std::array<int, 3>>& triangle_connectivity() const {
    return triangle_connectivity_;
  }

  const std::vector<std::array<int, 4>>& tetrahedron_connectivity() const {
    return tetrahedron_connectivity_;
  }

  const std::unordered_map<MeshID, std::array<MeshID, 4>>& element_adjacencies() const {
    return element_adjacencies_;
  }

  virtual SurfaceElementType get_surface_element_type(MeshID surface) const override {
    return SurfaceElementType::TRI; // hardcoded to Tri for this mock
  }

  virtual MeshID adjacent_element(MeshID element, int face) const override {
    return element_adjacencies_.at(element)[face];
  }

  virtual double element_volume(MeshID element) const override {
    const auto& conn = tetrahedron_connectivity()[element];
    std::array<Vertex, 4> verts = {
      vertices()[conn[0]],
      vertices()[conn[1]],
      vertices()[conn[2]],
      vertices()[conn[3]]
    };
    return tetrahedron_volume(verts);
  }

  inline int element_index(MeshID element) const override {
    return element;
  }

  inline MeshID element_id(size_t element_idx) const override {
    return static_cast<MeshID>(element_idx);
  }

  inline MeshID vertex_id(size_t vertex_idx) const override {
    return static_cast<MeshID>(vertex_idx);
  }

  inline int vertex_index(MeshID vertex) const override {
    return static_cast<int>(vertex);
  }

  virtual Vertex vertex_coordinates(MeshID vertex_id) const override {
    return vertices().at(vertex_id);
  }

  virtual std::vector<MeshID> element_connectivity(MeshID element) const override {
    return {tetrahedron_connectivity().at(element).begin(), tetrahedron_connectivity().at(element).end()};
  }

  // Other
  virtual MeshLibrary mesh_library() const override { return MeshLibrary::MOCK; }

// Data members
private:
  bool volumetric_elements_; // flag to indicate if the mesh has volumetric elements

  const BoundingBox bounding_box_ {-2.0, -3.0, -4.0, 5.0, 6.0, 7.0};

  std::unordered_map<MeshID, std::pair<MeshID, MeshID>> surface_sense_map_;
  std::unordered_map<MeshID, std::vector<MeshID>> volume_surfaces_map_;

  const std::vector<Vertex> vertices_ {
    // vertices in the upper z plane
    {bounding_box_.max_x, bounding_box_.min_y, bounding_box_.max_z},
    {bounding_box_.max_x, bounding_box_.max_y, bounding_box_.max_z},
    {bounding_box_.min_x, bounding_box_.max_y, bounding_box_.max_z},
    {bounding_box_.min_x, bounding_box_.min_y, bounding_box_.max_z},
    // vertices in the lower z plane
    {bounding_box_.max_x, bounding_box_.min_y, bounding_box_.min_z},
    {bounding_box_.max_x, bounding_box_.max_y, bounding_box_.min_z},
    {bounding_box_.min_x, bounding_box_.max_y, bounding_box_.min_z},
    {bounding_box_.min_x, bounding_box_.min_y, bounding_box_.min_z},
    {bounding_box_.center()} // bounding box center for tet elements
  };

    // tetrahedron connectivity for a cube, all elements connect to the center vertex
  // there are 12 tetrahedron in total
  const std::vector<std::array<int, 4>> tetrahedron_connectivity_ {
    // each tetrahedron is defined by 4 vertices, the last vertex is the center of the bounding box
    {0, 1, 2, 8}, // lower z plane
    {0, 2, 3, 8},
    {4, 6, 5, 8}, // upper z plane
    {4, 7, 6, 8},
    {0, 5, 1, 8}, // lower x plane
    {0, 4, 5, 8},
    {2, 6, 7, 8}, // upper x plane
    {2, 7, 3, 8},
    {0, 7, 4, 8}, // lower y plane
    {0, 3, 7, 8},
    {1, 5, 6, 8}, // upper y plane
    {1, 6, 2, 8}
  };

  // triangle connectivity for the exterior faces of the tetrahedron above
  const std::vector<std::array<int, 3>> triangle_connectivity_ {
    // Surface 0: upper z plane
    {0, 1, 2},
    {0, 2, 3},
    // Surface 1: lower z plane
    {4, 6, 5},
    {4, 7, 6},
    // Surface 2: upper x plane
    {0, 5, 1},
    {0, 4, 5},
    // Surface 3: lower x plane
    {2, 6, 7},
    {2, 7, 3},
    // Surface 4: upper y plane
    {1, 5, 6},
    {1, 6, 2},
    // Surface 5: lower y plane
    {0, 7, 4},
    {0, 3, 7}
  };

  // face-adjacency list for the tetrahedron elements
  std::unordered_map<MeshID, std::array<MeshID, 4>> element_adjacencies_ {
    {0, {-1, 1, 4, 11}},
    {1, {-1, 9, 0, 7}},
    {2, {-1, 5, 3, 10}},
    {3, {-1, 2, 8, 6}},
    {4, {-1, 0, 5, 10}},
    {5, {-1, 4, 8, 2}},
    {6, {-1, 7, 11, 3}},
    {7, {-1, 1, 6, 9}},
    {8, {-1, 5, 9, 3}},
    {9, {-1, 8, 1, 7}},
    {10, {-1, 11, 4, 2}},
    {11, {-1, 0, 10, 6}}
  };

};

struct MockElementFaceAccessor : public ElementFaceAccessor {
  MockElementFaceAccessor(const MeshMock* mesh_manager, MeshID element) :
  ElementFaceAccessor(element) {
    mesh_manager_ = mesh_manager;
    const auto& conn = mesh_manager->tetrahedron_connectivity()[element];
    tet_connectivity_ = mesh_manager->tet_faces(conn);
  }

  std::array<Vertex, 3> face_vertices(int i) const override {
    return {mesh_manager_->vertices()[tet_connectivity_[i][0]],
            mesh_manager_->vertices()[tet_connectivity_[i][1]],
            mesh_manager_->vertices()[tet_connectivity_[i][2]]};
  }

  // data members
  const MeshMock* mesh_manager_;
  std::array<std::array<int, 3>, 4> tet_connectivity_;
};
