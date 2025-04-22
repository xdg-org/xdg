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
    // determine the correct index to use
    int idx = 0;
    auto fe = face_data_.first_elements[idx];
    while(true) {
      if (tri - fe.first < fe.second) { break; }
      idx++;
      if (idx >= face_data_.first_elements.size()) { return false; }
      fe = face_data_.first_elements[idx];
    }
    return true;
  }

  //! \brief Get the coordinates of a triangle as MOAB CartVect's
  inline std::array<xdg::Vertex, 3> get_mb_coords(const EntityHandle& tri) {

    // determine the correct index to use
    int idx = 0;
    auto fe = face_data_.first_elements[idx];
    while(true) {
      if (tri - fe.first < fe.second) { break; }
      idx++;
      fe = face_data_.first_elements[idx];
    }

    size_t conn_idx = face_data_.element_stride * (tri - fe.first);
    size_t i0 = face_data_.vconn[idx][conn_idx] - 1;
    size_t i1 = face_data_.vconn[idx][conn_idx + 1] - 1;
    size_t i2 = face_data_.vconn[idx][conn_idx + 2] - 1;

    xdg::Vertex v0(tx_[idx][i0], ty_[idx][i0], tz_[idx][i0]);
    xdg::Vertex v1(tx_[idx][i1], ty_[idx][i1], tz_[idx][i1]);
    xdg::Vertex v2(tx_[idx][i2], ty_[idx][i2], tz_[idx][i2]);

    return {v0, v1, v2};
  }

  // Accessors
  //! \brief return the number of vertices being managed
  inline int n_vertices() { return num_vertices_; }

private:
  Interface* mbi {nullptr}; //!< MOAB instance for the managed data

  struct ConnectivityData {
    EntityType entity_type {MBMAXTYPE}; //!< Type of entity stored in this manager
    int num_entities {-1}; //!< Number of elements in the manager
    int element_stride {-1}; //!< Number of vertices used by each element
    std::vector<std::pair<EntityHandle, size_t>> first_elements; //!< Pairs of first element and length pairs for contiguous blocks of memory
    std::vector<const EntityHandle*> vconn; //!< Storage array(s) for the connectivity array

    void clear() {
      num_entities = -1;
      element_stride = -1;
      first_elements.clear();
      vconn.clear();
    }
  };

  ConnectivityData face_data_;
  ConnectivityData element_data_;


  int num_vertices_ {-1}; //!< Number of vertices in the manager
  std::vector<const double*> tx_; //!< Storage array(s) for vertex x coordinates
  std::vector<const double*> ty_; //!< Storage array(s) for vertex y coordinates
  std::vector<const double*> tz_; //!< Storage array(s) for vertex z coordinates
};

#endif // include guard
