#include <sstream>

// MOAB
#include "moab/Range.hpp"

#include "xdg/moab/direct_access.h"

MBDirectAccess::MBDirectAccess(Interface* mbi) : mbi(mbi) { setup(); }

void
MBDirectAccess::setup() {
  ErrorCode rval;

  // setup triangles
  Range tris;
  rval = mbi->get_entities_by_dimension(0, 2, tris, true);
  MB_CHK_SET_ERR_CONT(rval, "Failed to get all elements of dimension 2 (tris)");
  num_elements_ = tris.size();

  // only supporting triangle elements for now
  if (!tris.all_of_type(MBTRI)) { throw std::runtime_error("Not all 2D elements are triangles"); }

  moab::Range::iterator tris_it = tris.begin();
  while(tris_it != tris.end()) {
    // set connectivity pointer, element stride and the number of elements
    EntityHandle* conntmp;
    int n_elements;
    rval = mbi->connect_iterate(tris_it, tris.end(), conntmp, element_stride_, n_elements);
    MB_CHK_SET_ERR_CONT(rval, "Failed to get direct access to triangle elements");

    // set const pointers for the connectivity array and add first element/length pair to the set of first elements
    vconn_.push_back(conntmp);
    first_elements_.push_back({*tris_it, n_elements});

    // move iterator forward by the number of triangles in this contiguous memory block
    tris_it += n_elements;
  }

  // setup vertices
  Range verts;
  rval = mbi->get_entities_by_dimension(0, 0, verts, true);
  MB_CHK_SET_ERR_CONT(rval, "Failed to get all elements of dimension 0 (vertices)");
  num_vertices_ = verts.size();

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
    tx_.push_back(&(*xtmp));
    ty_.push_back(&(*ytmp));
    tz_.push_back(&(*ztmp));

    // move iterator forward by the number of vertices in this contiguous memory block
    verts_it += n_vertices;
  }
}

void
MBDirectAccess::clear()
{
  num_elements_ = -1;
  num_vertices_ = -1;
  element_stride_ = -1;

  first_elements_.clear();
  vconn_.clear();
  tx_.clear();
  ty_.clear();
  tz_.clear();
}

void
MBDirectAccess::update() {
  clear();
  setup();
}
