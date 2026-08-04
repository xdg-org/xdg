#include <sstream>

// MOAB
#include "moab/Range.hpp"

#include "xdg/moab/direct_access.h"

namespace xdg {

MBDirectAccess::MBDirectAccess(Interface* mbi)
: mbi(mbi)
{
  face_data_.emplace(SurfaceFaceType::TRI, ConnectivityData{});
  face_data_.at(SurfaceFaceType::TRI).entity_type = MBTRI;
  face_data_.emplace(SurfaceFaceType::QUAD, ConnectivityData{});
  face_data_.at(SurfaceFaceType::QUAD).entity_type = MBQUAD;

  element_data_.emplace(VolumeElementType::TET, ConnectivityData{});
  element_data_.at(VolumeElementType::TET).entity_type = MBTET;
  element_data_.emplace(VolumeElementType::HEX, ConnectivityData{});
  element_data_.at(VolumeElementType::HEX).entity_type = MBHEX;

  element_adjacency_data_.emplace(VolumeElementType::TET, AdjacencyData{});
  element_adjacency_data_.at(VolumeElementType::TET).entity_type = MBTET;
  element_adjacency_data_.emplace(VolumeElementType::HEX, AdjacencyData{});
  element_adjacency_data_.at(VolumeElementType::HEX).entity_type = MBHEX;
  setup();
}

void
MBDirectAccess::setup() {
  vertex_data_.setup(mbi);
  for (auto& [type, data] : face_data_) {
    data.setup(mbi);
  }
  for (auto& [type, data] : element_data_) {
    data.setup(mbi);
  }
  for (auto& [type, data] : element_adjacency_data_) {
    data.setup(mbi);
  }
  boundary_face_adjacency_data_.setup(mbi, face_data_);
}

void
MBDirectAccess::clear()
{
  vertex_data_.clear();
  for (auto& [type, data] : face_data_) {
    data.clear();
  }
  for (auto& [type, data] : element_data_) {
    data.clear();
  }
  for (auto& [type, data] : element_adjacency_data_) {
    data.clear();
  }
  boundary_face_adjacency_data_.clear();
}

void
MBDirectAccess::update() {
  clear();
  setup();
}

} // namespace xdg
