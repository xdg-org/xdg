
#ifndef BLOCK_MAPPING_HPP
#define BLOCK_MAPPING_HPP

#include <vector>
#include <algorithm>
#include <numeric>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace xdg {

template <typename ID, typename Index = int32_t>
struct Block {
    ID    id_start;   // First ID in this contiguous block
    Index idx_start;  // Element index corresponding to id_start
    Index count;      // Number of IDs in this block
};

template <typename ID, typename Index = int32_t>
class BlockMapping {
    static_assert(std::is_integral_v<ID>,    "Id must be an integral type");
    static_assert(std::is_integral_v<Index>, "Index must be an integral type");

public:
    static constexpr Index    invalid_index() { return Index(-1); }
    static constexpr ID       invalid_id()    { return ID(-1); }

    BlockMapping() = default;

    //! \brief Construct BlockMapping from a vector of element/vertex IDs
    //! \param ids vector of IDs in iteration order for the mesh
    //! \note IDs need not be sorted, but must be unique.
    BlockMapping(std::vector<ID>& ids)
    {
      if (ids.empty()) return;

      // Sort IDs to find contiguous blocks
      std::sort(ids.begin(), ids.end());

      size_t n = ids.size();
      size_t block_start = 0;
      Index current_idx = 0;

      for (size_t i = 1; i <= n; ++i) {
          if (i == n || ids[i] != ids[i - 1] + 1) {
              // End of a contiguous block
              Block<ID, Index> block;
              block.id_start  = ids[block_start];
              block.idx_start = current_idx;
              block.count     = static_cast<Index>(i - block_start);
              blocks_.push_back(block);

              current_idx += block.count;
              block_start = i;
          }
      }
    }

    // -----------------------------------------------------------------
    // ID -> Index mapping
    // Returns invalid_index() (i.e., -1) if the ID lies in no block.
    // -----------------------------------------------------------------
    Index id_to_index(ID id) const
    {
      if (blocks_.empty())
          return invalid_index();

      auto it = blocks_.begin();

      if (blocks_.size() > 1) {
        it = std::upper_bound(
            blocks_.begin(), blocks_.end(), id,
            [](ID value, const Block<ID, Index>& b) {
                return value < b.id_start;
            });
        --it; // last block with id_start <= id
      }

      // Compute offset within this block
      auto diff = id - it->id_start;

      // check if ID falls into a gap
      if (diff >= static_cast<Index>(it->count))
          return invalid_index();

      return it->idx_start + id - it->id_start;
    }

    // -----------------------------------------------------------------
    // Index -> ID mapping
    // Returns invalid_id() (i.e., -1) if the index lies in no block.
    // -----------------------------------------------------------------
    ID index_to_id(Index idx) const
    {
      if (blocks_.empty())
          return invalid_id();

      auto it = blocks_.begin();

      if (blocks_.size() > 1) {
        it = std::upper_bound(
            blocks_.begin(), blocks_.end(), idx,
            [&](Index value, const Block<ID, Index>& b) {
                return value < b.idx_start;
            });
        --it; // last block with idx_start <= idx
      }

      const auto& b = *it;

      return b.id_start + idx - b.idx_start;
    }

    // Expose blocks if needed for iteration/inspection
    const std::vector<Block<ID, Index>>& blocks() const { return blocks_; }

private:
    std::vector<Block<ID, Index>> blocks_;       // sorted by id_start
    std::vector<size_t>  order_by_idx_; // permutation sorted by idx_start
};

} // namespace xdg

#endif // BLOCK_MAPPING_HPP
