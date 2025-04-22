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
  face_data_.setup(mbi);
  element_data_.setup(mbi);
  vertex_data_.setup(mbi);
}

void
MBDirectAccess::clear()
{
  face_data_.clear();
  element_data_.clear();
  vertex_data_.clear();
}

void
MBDirectAccess::update() {
  clear();
  setup();
}
