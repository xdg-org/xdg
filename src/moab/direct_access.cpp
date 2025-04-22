#include <sstream>

// MOAB
#include "moab/Range.hpp"

#include "xdg/moab/direct_access.h"

MBDirectAccess::MBDirectAccess(Interface* mbi)
: mbi(mbi)
{
  face_data_.entity_type = MBTRI;
  element_data_.entity_type = MBTET;
  setup();
}

void
MBDirectAccess::setup() {
  ErrorCode rval;

  // setup face connectivity data
  Range faces;
  rval = mbi->get_entities_by_type(0, face_data_.entity_type, faces, true);
  MB_CHK_SET_ERR_CONT(rval, "Failed to get all elements of dimension 2 (faces)");
  face_data_.num_entities = faces.size();

  // only supporting triangle elements for now
  if (!faces.all_of_type(face_data_.entity_type)) { throw std::runtime_error("Not all 2D elements are triangles"); }

  moab::Range::iterator faces_it = faces.begin();
  while(faces_it != faces.end()) {
    // set connectivity pointer, element stride and the number of elements
    EntityHandle* conntmp;
    int n_elements;
    rval = mbi->connect_iterate(faces_it, faces.end(), conntmp, face_data_.element_stride, n_elements);
    MB_CHK_SET_ERR_CONT(rval, "Failed to get direct access to triangle elements");

    // set const pointers for the connectivity array and add first element/length pair to the set of first elements
    face_data_.vconn.push_back(conntmp);
    face_data_.first_elements.push_back({*faces_it, n_elements});

    // move iterator forward by the number of triangles in this contiguous memory block
    faces_it += n_elements;
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
  face_data_.clear();
  element_data_.clear();
  tx_.clear();
  ty_.clear();
  tz_.clear();
}

void
MBDirectAccess::update() {
  clear();
  setup();
}
