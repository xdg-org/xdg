#ifndef _MBDIRECTACCESS_
#define _MBDIRECTACCESS_

#include <array>
#include <memory>

// MOAB
#include "moab/Core.hpp"
#include "moab/CartVect.hpp"
#include "xdg/vec3da.h"

using namespace moab;

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

  //! \brief Get the coordinates of a triangle as MOAB CartVect's
  inline std::array<xdg::Vertex, 3> get_mb_coords(const EntityHandle& tri) {
    auto [block_idx, i0, i1, i2] = face_data_.get_connectivity_indices(tri);

    std::array<xdg::Vertex, 3> vertices;
    vertex_data_.set_coords(block_idx, i0, vertices[0]);
    vertex_data_.set_coords(block_idx, i1, vertices[1]);
    vertex_data_.set_coords(block_idx, i2, vertices[2]);
    return vertices;
  }

  //! \brief Get the coordinates of a triangle as MOAB CartVect's
  inline std::array<xdg::Vertex, 3> get_element_coords(const EntityHandle& element) {
    auto [block_idx, i0, i1, i2] = element_data_.get_connectivity_indices(element);

    std::array<xdg::Vertex, 3> vertices;
    vertex_data_.set_coords(block_idx, i0, vertices[0]);
    vertex_data_.set_coords(block_idx, i1, vertices[1]);
    vertex_data_.set_coords(block_idx, i2, vertices[2]);
    return vertices;
  }

  // Accessors
  //! \brief return the number of vertices being managed
  inline int n_vertices() { return vertex_data_.num_vertices; }

private:
  Interface* mbi {nullptr}; //!< MOAB instance for the managed data
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

    std::array<size_t, 4>
    get_connectivity_indices(const EntityHandle& e) {
      // determine the correct contiguous block index to use
      int block_idx = 0;
      auto fe = first_elements[block_idx];
      while(true) {
        if (e - fe.first < fe.second) { break; }
        block_idx++;
        fe = first_elements[block_idx];
      }

      std::array<size_t, 4> indices;
      indices[0] = block_idx;

      size_t conn_idx = element_stride * (e - fe.first);
      indices[1] = vconn[block_idx][conn_idx] - 1;
      indices[2] = vconn[block_idx][conn_idx + 1] - 1;
      indices[3] = vconn[block_idx][conn_idx + 2] - 1;
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

        // move iterator forward by the number of vertices in this contiguous memory block
        verts_it += n_vertices;
      }
    }

    void clear() {
      num_vertices = -1;
      tx.clear();
      ty.clear();
      tz.clear();
    }

    //! \brief Set the coordinates of a vertex based on an index into a contiguous block of memory
    //! \param idx The index of the block of memory
    //! \param i The index of the vertex in the block of memory
    //! \param v The vertex to set the coordinates of
    void set_coords(int idx, int i, xdg::Vertex& v) {
      v = xdg::Vertex(tx[idx][i], ty[idx][i], tz[idx][i]);
    }

    int num_vertices {-1}; //!< Number of vertices in the manager
    std::vector<const double*> tx; //!< Storage array(s) for vertex x coordinates
    std::vector<const double*> ty; //!< Storage array(s) for vertex y coordinates
    std::vector<const double*> tz; //!< Storage array(s) for vertex z coordinates
  };

  ConnectivityData face_data_;
  ConnectivityData element_data_;
  VertexData vertex_data_;
};

#endif // include guard
