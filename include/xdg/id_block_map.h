
#ifndef BLOCK_MAPPING_HPP
#define BLOCK_MAPPING_HPP

#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
#include <functional>
#include <numeric>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "xdg/constants.h"

namespace xdg {

template <typename ID, typename Index = MeshIndex>
struct IDBlock {
    ID    id_start;   // First ID in this contiguous block
    Index idx_start;  // Element index corresponding to id_start
    Index count;      // Number of IDs in this block
};

template <typename ID, typename Index = MeshIndex>
class IDBlockMapping {
    static_assert(std::is_integral_v<ID>,    "Id must be an integral type");
    static_assert(std::is_integral_v<Index>, "Index must be an integral type");

public:
    IDBlockMapping() = default;

    //! \brief Construct IDBlockMapping from a vector of element/vertex IDs
    //! \param ids vector of IDs in iteration order for the mesh
    //! \param converter Optional function to convert from input type to ID type,
    //!                  useful in the case where the mesh-native input identifiers
    //!                  differ from the desired ID type (typically MeshID)
    //! \note IDs are expected to be non-negative and monotoincally increasing,
    //!       but may contain gaps
    template<typename I = ID, typename T = std::vector<I>, typename Func = std::function<ID(const I&)>>
    IDBlockMapping(const T& ids, Func converter = [](const ID& x) { return x; })
    {
      if (ids.size() == 0) return;

      size_t n = ids.size();
      size_t block_start = 0;
      Index current_idx = 0;

      for (size_t i = 1; i <= n; ++i) {
        if (i == n || converter(ids[i]) != converter(ids[i-1]) + 1) {
          // End of a contiguous block
          IDBlock<ID, Index> block;
          block.id_start  = converter(ids[block_start]);
          block.idx_start = current_idx;
          block.count     = static_cast<Index>(i - block_start);
          blocks_.push_back(block);

          current_idx += block.count;
          block_start = i;
        }
        if (i < n && converter(ids[i]) <= converter(ids[i-1])) {
          std::cerr << "Error: IDs must be unique and monotonically increasing" << std::endl;
          std::exit(EXIT_FAILURE);
        }
      }
    }

    //! \brief Method that returns the index of an ID in the mapping
    //! \param id The ID to look up
    //! \return The index corresponding to the ID, or INDEX_NONE if not found
    Index id_to_index(ID id) const
    {
      if (blocks_.empty())
          return INDEX_NONE;

      auto it = blocks_.begin();
      if (blocks_.size() > 1) {
        // determine which block this ID falls into with binary search
        it = std::upper_bound(
            blocks_.begin(), blocks_.end(), id,
            [](ID value, const IDBlock<ID, Index>& b) {
                return value < b.id_start;
            });
        --it; // last block with id_start <= id
      }

      // Compute offset within this block
      const auto& block = *it;
      auto diff = id - block.id_start;

      // check if ID falls into a gap
      if (diff >= static_cast<Index>(block.count))
          return INDEX_NONE;

      return block.idx_start + diff;
    }

    //! \brief Method that returns the ID corresponding to an index
    //! \param idx The index to look up
    //! \return The ID corresponding to the index, or ID_NONE if not found
    ID index_to_id(Index idx) const
    {
      if (blocks_.empty())
          return ID_NONE;

      auto it = blocks_.begin();
      if (blocks_.size() > 1) {
        // determine which block this index falls into with binary search
        it = std::upper_bound(
            blocks_.begin(), blocks_.end(), idx,
            [&](Index value, const IDBlock<ID, Index>& b) {
                return value < b.idx_start;
            });
        --it;
      }

      const auto& block = *it;
      return block.id_start + idx - block.idx_start;
    }

    // Expose blocks if needed for iteration/inspection
    const std::vector<IDBlock<ID, Index>>& blocks() const { return blocks_; }

private:
    std::vector<IDBlock<ID, Index>> blocks_;       // sorted by id_start
};

} // namespace xdg

#endif // BLOCK_MAPPING_HPP
