
#ifndef _XDG_LIBMESH_SIDE_PAIR
#define _XDG_LIBMESH_SIDE_PAIR

#include "xdg/constants.h"

#include "libmesh/elem.h"

namespace xdg {

//! Helper class for unique identification of an element face
class SidePair {
public:
  // A helper struct to define a hash
  struct Hash {
    std::size_t operator()(const SidePair& p) const
    {
      auto hash1 = std::hash<const libMesh::Elem*>{}(p.first());
      auto hash2 = std::hash<int32_t>{}(p.side_num());
      // Combine the two hashes into a single hash value
      // This can be done using a common technique that combines them with XOR and bit shifting
      return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }
  };

  constexpr static int SIDE_NONE {-1};

  //! Constructors
  SidePair() = default;
  SidePair(std::pair<const libMesh::Elem*, int> old_side);
  SidePair(const libMesh::Elem* elem, int side_num);
  SidePair(const libMesh::Elem* elem1, const libMesh::Elem* elem2);
  SidePair(std::pair<const libMesh::Elem*, const libMesh::Elem*> side_pair);

  //! Mutators
  void set_order();
  void set_side_num();
  void swap();

  //! Operator overloads
  bool operator==(const SidePair& other) const;
  bool operator<(const SidePair& other) const;

  //! Accessors
  SurfaceFaceType face_type() const;
  const libMesh::Elem* first() const { return side_.first; }
  const libMesh::Elem* second() const { return side_.second; }
  int32_t side_num() const { return side_num_; }
  bool reverse_winding() const { return reverse_winding_; }

  //! Connectivity
  const std::unique_ptr<const libMesh::Elem> face_ptr() const;
  std::vector<MeshID> face_connectivity() const;
  std::vector<MeshID> face_vertices() const;

private:
  std::pair<const libMesh::Elem*, const libMesh::Elem*> side_ {nullptr, nullptr};
  int32_t side_num_ {SIDE_NONE};
  //! Whether second() should be used for fetching face vertices or not. If so, the
  //! winding order needs to be reversed to preserve the direction of the face normal.
  bool reverse_winding_ {false};
}; // class SidePair

} // namespace xdg

#endif // include guard
