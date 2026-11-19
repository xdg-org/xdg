#include "xdg/libmesh/side_pair.h"

#include "xdg/error.h"

#include "libmesh/cell_tet4.h"
#include "libmesh/cell_hex8.h"

namespace xdg {
SidePair::SidePair(std::pair<const libMesh::Elem*, int> old_side) {
  side_.first = old_side.first;
  side_num_ = old_side.second;
  side_.second = old_side.first->neighbor_ptr(side_num_);
  if (side_.second != nullptr) {
    set_order();
    set_side_num();
  }
}

SidePair::SidePair(const libMesh::Elem* elem, int side_num) {
  side_.first = elem;
  side_num_ = side_num;
  side_.second = elem->neighbor_ptr(side_num);
  if (side_.second != nullptr) {
    set_order();
    set_side_num();
  }
}

SidePair::SidePair(const libMesh::Elem* elem1, const libMesh::Elem* elem2) {
  side_.first = elem1;
  side_.second = elem2;
  if (elem1 == nullptr || elem2 == nullptr) {
    fatal_error("SidePair pointer constructor must be used with non-null pointers");
  }
  set_order();
  set_side_num();
}

SidePair::SidePair(std::pair<const libMesh::Elem*, const libMesh::Elem*> side_pair) : side_(side_pair) {
  if (side_.first == nullptr || side_.second == nullptr) {
    fatal_error("SidePair pair constructor must be used with non-null pointers");
  }
  set_order();
  set_side_num();
}

void SidePair::set_order() {
  if (side_.first == nullptr && side_.second == nullptr) {
    fatal_error("SidePair created with null elements");
  }

  // re-order so that null pointers come second if present
  if (side_.first == nullptr) {
    std::swap(side_.first, side_.second);
    return;
  }

  // if a null pointer is present, the remaining checks aren't necessary
  if (side_.second == nullptr) {
    return;
  }

  if (side_.first->id() == side_.second->id()) {
    fatal_error("SidePair created with the same element on both sides. Element ID: {}", side_.first->id());
  }

  if (side_.first->id() > side_.second->id()) {
    std::swap(side_.first, side_.second);
  }
}

SurfaceFaceType SidePair::face_type() const {
  switch (side_.first->type()) {
    case libMesh::TET4:
      return SurfaceFaceType::TRI;
    case libMesh::HEX8:
      return SurfaceFaceType::QUAD;
    default:
      return SurfaceFaceType::UNSUPPORTED;
  }
}

void SidePair::set_side_num() {
  reverse_winding_ = false;

  for (int i = 0; i < side_.first->n_sides(); i++) {
    if (side_.first->neighbor_ptr(i) == side_.second) {
      side_num_ = i;
      return;
    }
  }
  // If we've gotten here, we could be dealing with an AMR mesh. An element in first()
  // may be less refined then the element in second(), so we need to ensure faces are
  // quieried with second() and side_num_ is adjusted accordingly to point from second()
  // to first(). However, we need to signal that the winding order be reversed to ensure
  // the normals are consistent.
  auto unrefined_neighbor = side_.first->which_neighbor_am_i(side_.second);
  if (unrefined_neighbor != libMesh::invalid_uint) {
    // second() is a neighbor of first()
    reverse_winding_ = true;
    side_num_ = side_.second->which_neighbor_am_i(side_.first);
    return;
  }

  if (side_num_ == SIDE_NONE) {
    fatal_error("SidePair created with elements that are not neighbors");
  }
}

void SidePair::swap() {
  std::swap(side_.first, side_.second);
  set_side_num();
}

bool SidePair::operator==(const SidePair& other) const
{
  return first() == other.first() && side_num() == other.side_num() && second() == other.second();
}

bool SidePair::operator<(const SidePair& other) const
{
  return side_ < other.side_;
}

const std::unique_ptr<const libMesh::Elem> SidePair::face_ptr() const {
  if (reverse_winding()) {
    return second()->side_ptr(side_num());
  } else {
    return first()->side_ptr(side_num());
  }
}

std::vector<MeshID> SidePair::face_connectivity() const {
  auto fp = this->face_ptr();
  if (!fp) {
    fatal_error("Invalid face in SidePair");
  }

  std::vector<MeshID> conn;
  conn.reserve(fp->n_nodes());
  for (unsigned int i = 0; i < fp->n_nodes(); ++i) {
    conn.push_back(fp->node_id(i));
  }

  if (this->reverse_winding()) {
    std::reverse(conn.begin(), conn.end());
  }

  return std::move(conn);
}

std::vector<MeshID> SidePair::face_vertices() const {
  std::vector<MeshID> vertex_ids;
  const auto* elem = this->reverse_winding() ? this->second() : this->first();
  if (!elem) {
    fatal_error("Invalid face ID for LibMesh mesh");
  }
  const auto side_num = static_cast<unsigned int>(this->side_num());

  switch (elem->type()) {
    case libMesh::TET4: {
      vertex_ids.reserve(3);
      for (unsigned int i = 0; i < 3; ++i) {
        const auto node_ptr = elem->node_ptr(libMesh::Tet4::side_nodes_map[side_num][i]);
        vertex_ids.push_back(node_ptr->id());
      }
      break;
    }
    case libMesh::HEX8: {
      vertex_ids.reserve(4);
      for (unsigned int i = 0; i < 4; ++i) {
        const auto node_ptr = elem->node_ptr(libMesh::Hex8::side_nodes_map[side_num][i]);
        vertex_ids.push_back(node_ptr->id());
      }
      break;
    }
    default:
      fatal_error("Unsupported face element type {}", static_cast<int>(elem->type()));
  }

  // Need to reverse the winding order since pair.second() was used to fetch vertices.
  if (this->reverse_winding()) {
    std::reverse(vertex_ids.begin(), vertex_ids.end());
  }

  return std::move(vertex_ids);
}
}
