  #ifndef XDG_TESTING_MESH_MOCKS_H
#define XDG_TESTING_MESH_MOCKS_H

// Mock data for mesh interface testing
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/id_block_map.h"
#include "xdg/vec3da.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/element_face_accessor.h"

#include "xdg/geometry/measure.h"
#include "xdg/geometry/plucker.h"

using namespace xdg;

class MockedTriTetMesh : public MeshManager {
public:
  MockedTriTetMesh(bool volumetric_elements = true) : volumetric_elements_(volumetric_elements) {
    volumes_ = {0};
    surfaces_ = {0, 1, 2, 3, 4, 5};

    // Initialize some internal topology data structures
    volume_surfaces_map_[0] = surfaces_;
    for (int surface : surfaces_) {
      surface_sense_map_[surface] = {0, ID_NONE};
    }

    // Setup vertex, face, and element ID to index maps
    // each vertex and element is assigned a unique ID starting from 0
    std::vector<int> vertex_ids(vertices_.size());
    std::iota(vertex_ids.begin(), vertex_ids.end(), 0);
    vertex_id_map_ = IDBlockMapping<MeshID>(vertex_ids);

    std::vector<int> element_ids(tetrahedron_connectivity_.size());
    std::iota(element_ids.begin(), element_ids.end(), 0);
    volume_element_id_map_ = IDBlockMapping<MeshID>(element_ids);
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

  virtual std::vector<MeshID> face_vertices(MeshID element) const override {
    const auto& conn = triangle_connectivity()[element];
    return {conn[0], conn[1], conn[2]};
  }

  virtual std::vector<MeshID> face_connectivity(MeshID element) const override {
    const auto& conn = triangle_connectivity()[element];
    return {conn[0], conn[1], conn[2]};
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

  virtual SurfaceFaceType get_surface_face_type(MeshID surface) const override {
    return SurfaceFaceType::TRI; // hardcoded to Tri for this mock
  }

  virtual VolumeElementType get_volume_element_type(MeshID volume) const override {
    if (get_volume_elements(volume).empty()) {
      fatal_error("MockedTriTetMesh volume {} has no elements; cannot determine element type", volume);
    }
    return VolumeElementType::TET; // hardcoded to Tet for this mock
  }

  virtual MeshID adjacent_element(MeshID element, int face) const override {
    return element_adjacencies_.at(element)[face];
  }

  virtual Vertex vertex_coordinates(MeshID vertex_id) const override {
    return vertices().at(vertex_id);
  }

  virtual std::vector<MeshID> element_connectivity(MeshID element) const override {
    return {tetrahedron_connectivity().at(element).begin(), tetrahedron_connectivity().at(element).end()};
  }

  virtual MeshID get_boundary_face_element(MeshID face) const override {
    static const std::array<MeshID, 12> boundary_face_elements {{
      0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 8, 9
    }};
    if (face < 0 || face >= static_cast<MeshID>(boundary_face_elements.size())) return ID_NONE;
    return boundary_face_elements.at(face);
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
  MockElementFaceAccessor(const MockedTriTetMesh* mesh_manager, MeshID element) :
  ElementFaceAccessor(element) {
    mesh_manager_ = mesh_manager;
    const auto& conn = mesh_manager->tetrahedron_connectivity()[element];
    tet_connectivity_ = mesh_manager->tet_faces(conn);
  }

  std::vector<Vertex> face_vertices(int i) const override {
    return {mesh_manager_->vertices()[tet_connectivity_[i][0]],
            mesh_manager_->vertices()[tet_connectivity_[i][1]],
            mesh_manager_->vertices()[tet_connectivity_[i][2]]};
  }

  int num_faces() const override {
    return 4;
  }

  // data members
  const MockedTriTetMesh* mesh_manager_;
  std::array<std::array<int, 3>, 4> tet_connectivity_;
};

using MeshMock = MockedTriTetMesh;

namespace xdg {

// A lightweight two-element hexahedral mesh.
class MockedQuadHexMesh : public MeshManager {
public:
  MockedQuadHexMesh() {
    volumes_ = {0};
    surfaces_ = {0, 1, 2, 3, 4, 5};
    volume_surfaces_map_[0] = surfaces_;
    for (const auto surface : surfaces_) {
      surface_sense_map_[surface] = {0, ID_NONE};
    }

    // Setup vertex, face, and element ID to index maps
    // each vertex and element is assigned a unique ID starting from 0
    std::vector<int> vertex_ids(vertices_.size());
    std::iota(vertex_ids.begin(), vertex_ids.end(), 0);
    vertex_id_map_ = IDBlockMapping<MeshID>(vertex_ids);

    std::vector<int> element_ids(hexahedron_connectivity_.size());
    std::iota(element_ids.begin(), element_ids.end(), 0);
    volume_element_id_map_ = IDBlockMapping<MeshID>(element_ids);
  }

  void load_file(const std::string& /*file_name*/) override {}
  void init() override {}

  int num_volumes() const override { return 1; }
  int num_surfaces() const override { return static_cast<int>(surfaces_.size()); }
  int num_ents_of_dimension(int dim) const override {
    if (dim == 2) return num_surfaces();
    if (dim == 3) return num_volumes();
    return 0;
  }

  int num_volume_elements(MeshID /*volume*/) const override {
    return static_cast<int>(hexahedron_connectivity_.size());
  }
  int num_volume_faces(MeshID volume) const override {
    int count = 0;
    for (auto surf : get_volume_surfaces(volume)) {
      count += num_surface_faces(surf);
    }
    return count;
  }
  int num_surface_faces(MeshID surface) const override {
    return static_cast<int>(surface_faces_.at(surface).size());
  }
  int num_vertices() const override { return static_cast<int>(vertices_.size()); }

  std::vector<MeshID> get_volume_elements(MeshID /*volume*/) const override { return {0, 1}; }

  std::vector<MeshID> get_surface_faces(MeshID surface) const override {
    return surface_faces_.at(surface);
  }

  std::vector<Vertex> element_vertices(MeshID element) const override {
    const auto& connectivity = hexahedron_connectivity_.at(element);
    std::vector<Vertex> element_vertices;
    element_vertices.reserve(connectivity.size());
    for (const auto vertex : connectivity) {
      element_vertices.push_back(vertices_.at(vertex));
    }
    return element_vertices;
  }

  std::vector<MeshID> face_vertices(MeshID face) const override {
    return face_connectivity_.at(face);
  }

  std::vector<MeshID> face_connectivity(MeshID face) const override {
    return face_connectivity_.at(face);
  }

  SurfaceFaceType get_surface_face_type(MeshID surface) const override {
    auto faces = get_surface_faces(surface);
    for (const auto& face : faces) {
      if (face_connectivity_.at(face).size() == 4) return SurfaceFaceType::QUAD;
    }
    return SurfaceFaceType::TRI;
  }

  VolumeElementType get_volume_element_type(MeshID volume) const override {
    if (get_volume_elements(volume).empty()) {
      fatal_error("MockedQuadHexMesh volume {} has no elements; cannot determine element type", volume);
    }
    return VolumeElementType::HEX;
  }

  MeshID adjacent_element(MeshID element, int face) const override {
    return element_adjacencies_.at(element).at(face);
  }

  Vertex vertex_coordinates(MeshID vertex_id) const override {
    return vertices_.at(vertex_id);
  }

  std::vector<MeshID> element_connectivity(MeshID element) const override {
    const auto& connectivity = hexahedron_connectivity_.at(element);
    return {connectivity.begin(), connectivity.end()};
  }

  MeshID get_boundary_face_element(MeshID face) const override {
    static const std::array<MeshID, 10> boundary_face_elements {{
      0, 1, 0, 1, 0, 1, 1, 0, 1, 0
    }};
    if (face < 0 || face >= static_cast<MeshID>(boundary_face_elements.size())) return ID_NONE;
    return boundary_face_elements.at(face);
  }

  std::vector<MeshID> get_volume_surfaces(MeshID volume) const override {
    auto it = volume_surfaces_map_.find(volume);
    if (it == volume_surfaces_map_.end()) return {};
    return it->second;
  }

  std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override {
    return surface_sense_map_.at(surface);
  }

  Sense surface_sense(MeshID surface, MeshID volume) const override {
    auto senses = surface_senses(surface);
    if (volume == senses.first) return Sense::FORWARD;
    if (volume == senses.second) return Sense::REVERSE;
    fatal_error("Volume {} is not a parent of surface {}", volume, surface);
    return Sense::UNSET;
  }

  MeshID create_volume() override { return next_volume_id(); }

  void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override {
    auto& surfaces = volume_surfaces_map_[volume];
    if (!overwrite && std::find(surfaces.begin(), surfaces.end(), surface) != surfaces.end()) {
      fatal_error("Surface {} already exists in volume {}", surface, volume);
    }
    surfaces.push_back(surface);
    auto& senses = surface_sense_map_[surface];
    if (sense == Sense::FORWARD) senses.first = volume;
    else if (sense == Sense::REVERSE) senses.second = volume;
  }

  void parse_metadata() override {}

  MeshLibrary mesh_library() const override { return MeshLibrary::MOCK; }

private:
  std::unordered_map<MeshID, std::pair<MeshID, MeshID>> surface_sense_map_;
  std::unordered_map<MeshID, std::vector<MeshID>> volume_surfaces_map_;

  const std::vector<Vertex> vertices_ {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {1.0, 1.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0},
    {1.0, 0.0, 1.0},
    {1.0, 1.0, 1.0},
    {0.0, 1.0, 1.0},
    {2.0, 0.0, 0.0},
    {2.0, 1.0, 0.0},
    {2.0, 0.0, 1.0},
    {2.0, 1.0, 1.0},
  };

  // Two unit-cube hexahedra using MBCN vertex ordering.
  const std::vector<std::array<int, 8>> hexahedron_connectivity_ {
    {0, 1, 2, 3, 4, 5, 6, 7},
    {1, 8, 9, 2, 5, 10, 11, 6},
  };

  // Exterior faces use counter-clockwise ordering when viewed from outside the volume.
  const std::unordered_map<MeshID, std::vector<MeshID>> face_connectivity_ {
    {0, {4, 5, 6, 7}},    // top, element 0 (+Z)
    {1, {5, 10, 11, 6}},  // top, element 1 (+Z)
    {2, {0, 3, 2, 1}},    // bottom, element 0 (-Z)
    {3, {1, 2, 9, 8}},    // bottom, element 1 (-Z)
    {4, {0, 1, 5, 4}},    // front, element 0 (-Y)
    {5, {1, 8, 10, 5}},   // front, element 1 (-Y)
    {6, {8, 9, 11, 10}},  // right (+X)
    {7, {2, 3, 7, 6}},    // back, element 0 (+Y)
    {8, {9, 2, 6, 11}},   // back, element 1 (+Y)
    {9, {3, 0, 4, 7}},    // left (-X)
  };

  const std::unordered_map<MeshID, std::vector<MeshID>> surface_faces_ {
    {0, {0, 1}},
    {1, {2, 3}},
    {2, {4, 5}},
    {3, {6}},
    {4, {7, 8}},
    {5, {9}},
  };

  // Local face order: bottom, top, front, right, back, left.
  const std::unordered_map<MeshID, std::array<MeshID, 6>> element_adjacencies_ {
    {0, {ID_NONE, ID_NONE, ID_NONE, 1, ID_NONE, ID_NONE}},
    {1, {ID_NONE, ID_NONE, ID_NONE, ID_NONE, ID_NONE, 0}},
  };
};

struct MockHexElementFaceAccessor : public ElementFaceAccessor {
  MockHexElementFaceAccessor(const MockedQuadHexMesh* mesh_manager, MeshID element)
    : ElementFaceAccessor(element), mesh_manager_(mesh_manager)
  {
    const auto connectivity = mesh_manager_->element_connectivity(element);
    for (size_t i = 0; i < connectivity.size(); ++i) {
      element_vertices_[i] = mesh_manager_->vertex_coordinates(connectivity[i]);
    }
  }

  std::vector<Vertex> face_vertices(int i) const override {
    static constexpr std::array<std::array<int, 4>, 6> face_ordering {
      std::array<int, 4> {0, 3, 2, 1},
      std::array<int, 4> {4, 5, 6, 7},
      std::array<int, 4> {0, 1, 5, 4},
      std::array<int, 4> {1, 2, 6, 5},
      std::array<int, 4> {2, 3, 7, 6},
      std::array<int, 4> {3, 0, 4, 7},
    };

    const auto& face = face_ordering.at(i);
    return {
      element_vertices_[face[0]],
      element_vertices_[face[1]],
      element_vertices_[face[2]],
      element_vertices_[face[3]],
    };
  }

  int num_faces() const override { return 6; }

  const MockedQuadHexMesh* mesh_manager_;
  std::array<Vertex, 8> element_vertices_;
};

} // namespace xdg

#endif // XDG_TESTING_MESH_MOCKS_H
