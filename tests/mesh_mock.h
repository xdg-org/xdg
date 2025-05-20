#include <algorithm>
#include <unordered_map>

#include "xdg/bbox.h"
#include "xdg/constants.h"
#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/mesh_manager_interface.h"

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

  virtual int num_vertices() const override {
    return vertices_.size();
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

  virtual MeshID element_volume_id(MeshID element) const override {
    return 1;
  }

  virtual std::array<Vertex, 3> face_vertices(MeshID element) const override {
    const auto& conn = triangle_connectivity()[element];
    return {vertices()[conn[0]], vertices()[conn[1]], vertices()[conn[2]]};
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

  std::pair<MeshID, double>
  next_element(MeshID current_element,
                const Position& r,
                const Position& u) const override
  {
    // get the tetrahedron element
    const auto& elem_ref = tetrahedron_connectivity()[current_element];

    std::array<double, 4> dists = {INFTY, INFTY, INFTY, INFTY};
    std::array<bool, 4> hit_types = {false, false, false, false};

    auto tet_face_conn = tet_faces(elem_ref);

    // get the faces (triangles) of this element
    for (int i = 0; i < 4; i++) {
      // triangle connectivity
      std::array<Position, 3> coords;
      for (int j = 0; j < 3; j++) {
        coords[j] = vertices()[tet_face_conn[i][j]];
      }

      // get the normal of the triangle
      const Position v1 = coords[1] - coords[0];
      const Position v2 = coords[2] - coords[0];

      const Position normal = (v1.cross(v2)).normalize();

      if (normal.dot(u) < 0.0) {
        // the ray is not exiting this face
        continue;
      }

      // perform ray-triangle intersection
      hit_types[i] = plucker_ray_tri_intersect(coords, r, u, dists[i]);

      if (dists[i] < 0.0) {
        hit_types[i] = false;
        dists[i] = INFTY;
      }
    }

    // determine the minimum distance to exit and the face number
    int idx_out = -1;
    double min_dist = INFTY;
    for (int i = 0; i < dists.size(); i++) {
      if (!hit_types[i])
        continue;
      if (dists[i] < min_dist) {
        min_dist = dists[i];
        idx_out = i;
      }
    }

    if (idx_out == -1) {

      fatal_error(fmt::format("No exit found in element {}", current_element));
    }

    MeshID next_element = element_adjacencies().at(current_element)[idx_out];
    return {next_element, min_dist};
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

  // Other
  virtual MeshLibrary mesh_library() const override { return MeshLibrary::INTERNAL; }

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
    {0, 1, 2},
    {0, 2, 3},
    {4, 6, 5},
    {4, 7, 6},
    {0, 5, 1},
    {0, 4, 5},
    {2, 6, 7},
    {2, 7, 3},
    {0, 7, 4},
    {0, 3, 7},
    {1, 5, 6},
    {1, 6, 2}
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