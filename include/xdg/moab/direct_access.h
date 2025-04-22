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
    auto fe = first_elements_[idx];
    while(true) {
      if (tri - fe.first < fe.second) { break; }
      idx++;
      if (idx >= first_elements_.size()) { return false; }
      fe = first_elements_[idx];
    }
    return true;
  }

  //! \brief Get the coordinates of a triangle as MOAB CartVect's
  inline std::array<xdg::Vertex, 3> get_mb_coords(const EntityHandle& tri) {

    // determine the correct index to use
    int idx = 0;
    auto fe = first_elements_[idx];
    while(true) {
      if (tri - fe.first < fe.second) { break; }
      idx++;
      fe = first_elements_[idx];
    }

    size_t conn_idx = element_stride_ * (tri - fe.first);
    size_t i0 = vconn_[idx][conn_idx] - 1;
    size_t i1 = vconn_[idx][conn_idx + 1] - 1;
    size_t i2 = vconn_[idx][conn_idx + 2] - 1;

    xdg::Vertex v0(tx_[idx][i0], ty_[idx][i0], tz_[idx][i0]);
    xdg::Vertex v1(tx_[idx][i1], ty_[idx][i1], tz_[idx][i1]);
    xdg::Vertex v2(tx_[idx][i2], ty_[idx][i2], tz_[idx][i2]);

    return {v0, v1, v2};
  }

  // //! \brief Get the coordinates of a triangle as Vec3da's
  // inline std::array<Vec3da, 3> get_coords(const EntityHandle& tri) {

  //   // determine the correct index to use
  //   int idx = 0;
  //   auto fe = first_elements_[idx];
  //   while(true) {
  //     if (tri - fe.first < fe.second) { break; }
  //     idx++;
  //     fe = first_elements_[idx];
  //   }

  //   size_t conn_idx = element_stride_ * (tri - fe.first);
  //   size_t i0 = vconn_[idx][conn_idx] - 1;
  //   size_t i1 = vconn_[idx][conn_idx + 1] - 1;
  //   size_t i2 = vconn_[idx][conn_idx + 2] - 1;

  //   Vec3da v0(tx_[idx][i0], ty_[idx][i0], tz_[idx][i0]);
  //   Vec3da v1(tx_[idx][i1], ty_[idx][i1], tz_[idx][i1]);
  //   Vec3da v2(tx_[idx][i2], ty_[idx][i2], tz_[idx][i2]);

  //   return {v0, v1, v2};
  // }

  // Accessors
  //! \brief return the number of elements being managed
  inline int n_elements() { return num_elements_; }
  //! \brief return the number of vertices being managed
  inline int n_vertices() { return num_vertices_; }
  //! \brief return the stride between elements in the coordinate arrays
  inline int stride() { return element_stride_;}

private:
  Interface* mbi {nullptr}; //!< MOAB instance for the managed data
  int num_elements_ {-1}; //!< Number of elements in the manager
  int num_vertices_ {-1}; //!< Number of vertices in the manager
  int element_stride_ {-1}; //!< Number of vertices used by each element
  std::vector<std::pair<EntityHandle, size_t>> first_elements_; //!< Pairs of first element and length pairs for contiguous blocks of memory
  std::vector<const EntityHandle*> vconn_; //!< Storage array(s) for the connectivity array
  std::vector<double*> tx_; //!< Storage array(s) for vertex x coordinates
  std::vector<double*> ty_; //!< Storage array(s) for vertex y coordinates
  std::vector<double*> tz_; //!< Storage array(s) for vertex z coordinates
};

#endif // include guard
