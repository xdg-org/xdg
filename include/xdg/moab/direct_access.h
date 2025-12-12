#ifndef _MBDIRECTACCESS_
#define _MBDIRECTACCESS_

#include <array>
#include <memory>

// MOAB
#include "moab/Core.hpp"
#include "moab/CartVect.hpp"
#include "xdg/constants.h"
#include "xdg/vec3da.h"


using namespace moab;

namespace xdg {

/*! Class to manage direct access of triangle connectivity and coordinates */
class MBDirectAccess {

public:
  // constructor
  MBDirectAccess(Interface* mbi);

  //! \brief Initialize internal structures
  void setup();

  //! \brief Reset internal data structures, but maintain MOAB isntance
  void clear();

  //! \brief Update internal data structures to account for changes in the MOAB instance
  void update();

  //! \brief Check that a triangle is part of the managed coordinates here
  inline bool accessible(EntityHandle tri) {
    // determine the correct contiguous memory block index to use
    int block_idx = 0;
    auto fe = face_data_.first_elements[block_idx];
    while(true) {
      if (tri - fe.first < fe.second) { break; }
      block_idx++;
      if (block_idx >= face_data_.first_elements.size()) { return false; }
      fe = face_data_.first_elements[block_idx];
    }
    return true;
  }

  //! \brief Determine the index of an element in the managed data
  inline size_t element_index(EntityHandle element) {
    size_t element_index = 0;
    auto fe = element_data_.first_elements.begin();
    while(true) {
      if (element - fe->first < fe->second) { break; }
      element_index += fe->second;
      fe++;
      if (fe == element_data_.first_elements.end()) {
        throw std::runtime_error("Element not found in MBDirectAccess::element_index");
      }
    }
    element_index += element - fe->first;
    return element_index;
  }

  //! \brief Determine the element handle of an element based on its index in
  //! the managed data
  inline EntityHandle element_handle(size_t index) {
    size_t running_index = 0;
    auto fe = element_data_.first_elements.begin();
    while(true) {
      if (index < running_index + fe->second) {
        return fe->first + (index - running_index);
      }
      running_index += fe->second;
      fe++;
      if (fe == element_data_.first_elements.end()) {
        throw std::runtime_error("Index out of range in MBDirectAccess::element_handle");
      }
    }
  }

  //! \brief Get the coordinates of a triangle as XDG Vertices
  inline std::array<xdg::Vertex, 3> get_mb_coords(const EntityHandle& tri) {
    auto [i0, i1, i2] = face_data_.get_connectivity_indices<3>(tri);

    std::array<xdg::Vertex, 3> vertices;
    vertex_data_.set_coords(i0, vertices[0]);
    vertex_data_.set_coords(i1, vertices[1]);
    vertex_data_.set_coords(i2, vertices[2]);
    return vertices;
  }

  //! \brief Get the coordinates of a triangle as XDG Vertices
  inline std::array<xdg::Vertex, 4> get_element_coords(const EntityHandle& element) {
    auto [i0, i1, i2, i3] = element_data_.get_connectivity_indices<4>(element);

    std::array<xdg::Vertex, 4> vertices;
    vertex_data_.set_coords(i0, vertices[0]);
    vertex_data_.set_coords(i1, vertices[1]);
    vertex_data_.set_coords(i2, vertices[2]);
    vertex_data_.set_coords(i3, vertices[3]);
    return vertices;
  }

  int vertex_index(EntityHandle vertex) {
    int vertex_index = 0;
    auto fe = vertex_data_.first_vertices.begin();
    while(true) {
      if (vertex - fe->first < fe->second) { break; }
      vertex_index += fe->second;
      fe++;
      if (fe == vertex_data_.first_vertices.end()) {
        throw std::runtime_error("Vertex not found in MBDirectAccess::vertex_index");
      }
    }
    vertex_index += vertex - fe->first;
    return vertex_index;
  }

  EntityHandle vertex_handle(int vertex_index) {
    int running_index = 0;
    auto fe = vertex_data_.first_vertices.begin();
    while(true) {
      if (vertex_index < running_index + fe->second) {
        return fe->first + (vertex_index - running_index);
      }
      running_index += fe->second;
      fe++;
      if (fe == vertex_data_.first_vertices.end()) {
        throw std::runtime_error("Index out of range in MBDirectAccess::vertex_handle");
      }
    }
  }

  //! \brief Get the adjacent element
  inline EntityHandle get_adjacent_element(const EntityHandle& element, int face_number) {
    return element_adjacency_data_.get_adjacent_element(element, face_number);
  }

  inline std::vector<EntityHandle> get_element_adjacencies(const EntityHandle& element) {
    return element_adjacency_data_.get_element_adjacencies(element);
  }

  const std::vector<std::vector<int>>& get_face_ordering(EntityType entity_type) const {
    return element_adjacency_data_.ordering.at(entity_type);
  }

  // Accessors
  //! \brief return the number of vertices being managed
  inline int n_vertices() { return vertex_data_.num_vertices; }

private:
  Interface* mbi {nullptr}; //!< MOAB instance for the managed data

  struct AdjacencyData {
    EntityType entity_type {MBTET}; //!< Type of entity stored in this manager
    int num_entities {-1}; //!< Number of elements in the manager
    std::unordered_map<EntityHandle, std::vector<EntityHandle>> adj_info_;

    void setup(Interface* mbi) {
      ErrorCode rval;

      // setup element adjacenty data
      // TODO: support other element types
      Range elements;
      rval = mbi->get_entities_by_type(0, entity_type, elements, true);
      MB_CHK_SET_ERR_CONT(rval, "Failed to get MOAB element adjacencies");

      const auto& ord = ordering[entity_type];
      // loop over elements and setup adjacency data
      for (auto element : elements) {
        adj_info_[element] = std::vector<EntityHandle>(ord.size(), xdg::ID_NONE);
        // get element connectivity
        std::vector<EntityHandle> conn;
        rval = mbi->get_connectivity(&element, 1, conn);

        // use ordering and adjacency call with intersection to populate adjacency entry
        for (int i = 0; i < ord.size(); i++) {
          auto o = ord[i];
          // determine element for this face
          std::vector<EntityHandle> verts;
          for (auto idx : o) verts.push_back(conn[idx]);
          Range adj_ents;
          rval = mbi->get_adjacencies(verts.data(), verts.size(), 3, true, adj_ents);

          // there be at most two adjacent elements for a given face
          if (adj_ents.size() > 2) {
            throw std::runtime_error("Something went wrong gathering adjacent face");
          }

          // if only one adjacent element, the face is on a boundary
          if (adj_ents.size() == 1) {
            // in this case, the returned element must be the current element itself
            if (adj_ents[0] != element) {
              throw std::runtime_error("The face is on a boundary, but the returned adjacent element is not the current element");
            }
            // if this face is on the boundary, there is no adjacency to add. Move on to the next face
            continue;
          }

          if (adj_ents.size() == 2) {
            if (adj_ents[0] == element) {
            adj_info_[element][i] = adj_ents[1];
          } else {
            adj_info_[element][i] = adj_ents[0];
          }
          }
        }
      }
    }

    EntityHandle get_adjacent_element(const EntityHandle& element, int face_number) {
      return adj_info_[element][face_number];
    }

    std::vector<EntityHandle> get_element_adjacencies(const EntityHandle& element) {
      return adj_info_[element];
    }

    void clear() {
      num_entities = -1;
      adj_info_.clear();
    }

    // ordering of element faces based on the cannonical ordering descibed here:
    // Canonical numbering systems for finite‐element codes (http://dx.doi.org/10.1002/cnm.1237)
    std::unordered_map<EntityType, std::vector<std::vector<int>>> ordering = {
    {MBTET, {{0, 1, 3}, {1, 2, 3}, {2, 0, 3}, {0, 2, 1}}}
    };
  };
  struct ConnectivityData {
    EntityType entity_type {MBMAXTYPE}; //!< Type of entity stored in this manager
    int num_entities {-1}; //!< Number of elements in the manager
    int element_stride {-1}; //!< Number of vertices used by each element
    std::vector<std::pair<EntityHandle, size_t>> first_elements; //!< Pairs of first element and length pairs for contiguous blocks of memory
    std::vector<const EntityHandle*> vconn; //!< Storage array(s) for the connectivity array

    void setup(Interface * mbi) {
      ErrorCode rval;

      // setup face connectivity data
      Range faces;
      rval = mbi->get_entities_by_type(0, entity_type, faces, true);
      MB_CHK_SET_ERR_CONT(rval, "Failed to get all elements of dimension 2 (faces)");
      num_entities = faces.size();

      // only supporting triangle elements for now
      if (!faces.all_of_type(entity_type)) { throw std::runtime_error("Not all 2D elements are triangles"); }

      moab::Range::iterator faces_it = faces.begin();
      while(faces_it != faces.end()) {
        // set connectivity pointer, element stride and the number of elements
        EntityHandle* conntmp;
        int n_elements;
        rval = mbi->connect_iterate(faces_it, faces.end(), conntmp, element_stride, n_elements);
        MB_CHK_SET_ERR_CONT(rval, "Failed to get direct access to triangle elements");

        // set const pointers for the connectivity array and add first element/length pair to the set of first elements
        vconn.push_back(conntmp);
        first_elements.push_back({*faces_it, n_elements});

        // move iterator forward by the number of triangles in this contiguous memory block
        faces_it += n_elements;
      }
    }

    template <int N>
    std::array<size_t, N>
    get_connectivity_indices(const EntityHandle& e) {
      // determine the correct contiguous block index to use
      int block_idx = 0;
      auto fe = first_elements[block_idx];
      while(true) {
        if (e - fe.first < fe.second) { break; }
        block_idx++;
        fe = first_elements[block_idx];
      }

      std::array<size_t, N> indices;
      size_t conn_idx = element_stride * (e - fe.first);
      for (int i = 0; i < N; i++) {
        indices[i] = vconn[block_idx][conn_idx + i];
      }
      return indices;
    }

    void clear() {
      num_entities = -1;
      element_stride = -1;
      first_elements.clear();
      vconn.clear();
    }
  };

  struct VertexData {
    void setup(Interface* mbi) {
      ErrorCode rval;
      // setup vertices
      Range verts;
      rval = mbi->get_entities_by_dimension(0, 0, verts, true);
      MB_CHK_SET_ERR_CONT(rval, "Failed to get all elements of dimension 0 (vertices)");
      num_vertices = verts.size();

      moab::Range::iterator verts_it = verts.begin();
      while (verts_it != verts.end()) {
        // set vertex coordinate pointers
        double* xtmp;
        double* ytmp;
        double* ztmp;
        int n_vertices;
        rval = mbi->coords_iterate(verts_it, verts.end(), xtmp, ytmp, ztmp, n_vertices);
        MB_CHK_SET_ERR_CONT(rval, "Failed to get direct access to vertex elements");

        // add the vertex coordinate arrays to their corresponding vector of array pointers
        tx.push_back(&(*xtmp));
        ty.push_back(&(*ytmp));
        tz.push_back(&(*ztmp));

        first_vertices.push_back({*verts_it, n_vertices});

        // move iterator forward by the number of vertices in this contiguous memory block
        verts_it += n_vertices;
      }
    }

    void clear() {
      num_vertices = -1;
      tx.clear();
      ty.clear();
      tz.clear();
      first_vertices.clear();
    }

    //! \brief Set the coordinates of a vertex based on an index into a contiguous block of memory
    //! \param i The index of the vertex in the block of memory
    //! \param v The vertex to set the coordinates of
    void set_coords(int i, xdg::Vertex& v) {
      // determine the correct contiguous memory block index to use
      int idx = 0;
      auto fe = first_vertices[idx];
      while(i - fe.first >= fe.second) {
        fe = first_vertices[++idx];
      }
      // determine index into the contiguous block of coordinate memory
      i -= fe.first;
      // populate the vertex reference with coordinates
      v = xdg::Vertex(tx[idx][i], ty[idx][i], tz[idx][i]);
    }

    int num_vertices {-1}; //!< Number of vertices in the manager
    std::vector<const double*> tx; //!< Storage array(s) for vertex x coordinates
    std::vector<const double*> ty; //!< Storage array(s) for vertex y coordinates
    std::vector<const double*> tz; //!< Storage array(s) for vertex z coordinates
    std::vector<std::pair<EntityHandle, size_t>> first_vertices; //!< Pairs of first vertex and length pairs for contiguous blocks of memory
  };

  ConnectivityData face_data_;
  ConnectivityData element_data_;
  AdjacencyData element_adjacency_data_;
  VertexData vertex_data_;
};

} // namespace xdg

#endif // include guard
