#include "./BitmaskTree.hpp"
#include "SlabAllocator.hpp"
#include <limits>

class PagedBitmaskTree {
private:
  using TreeAllocator = SlabAllocator<BitmaskTree, 15>;

  uint32_t NULL_INDEX = std::numeric_limits<uint32_t>::max();
  uint32_t slots[64] = {0};

  uint64_t active_chunk{0}; // bitmask

public:
  PagedBitmaskTree() {
    for (int i = 0; i < 64; i++)
      slots[i] = NULL_INDEX;
  }

  void setActive(uint32_t price, TreeAllocator &tree_alloc) noexcept {
    uint32_t chunk_index = price >> 18;
    uint32_t local_price = price & ((1ULL << 18) - 1);

    if (__builtin_expect(slots[chunk_index] == NULL_INDEX, 0)) {
      slots[chunk_index] = tree_alloc.allocate();
    }

    active_chunk |= (1ULL << chunk_index);
    BitmaskTree &tree = tree_alloc.get(slots[chunk_index]);
    tree.set_active(local_price);
  }

  void clearActive(uint32_t price, TreeAllocator &tree_alloc) noexcept {
    uint32_t chunk_index = price >> 18;
    uint32_t local_price = price & ((1ULL << 18) - 1);

    BitmaskTree &tree = tree_alloc.get(slots[chunk_index]);
    tree.clear_active(local_price);

    if (tree.isEmpty()) {
      active_chunk &= ~(1ULL << chunk_index);
    }
  }

  uint32_t getBestBid(TreeAllocator &tree_alloc) noexcept {
    if (active_chunk == 0) {
      return 0;
    }

    uint32_t best_chunk_index = 63 - __builtin_clzll(active_chunk);
    BitmaskTree &tree = tree_alloc.get(slots[best_chunk_index]);
    uint32_t local_best = tree.get_best_bid();

    return (best_chunk_index << 18) | local_best;
  }

  uint32_t getBestAsk(TreeAllocator &tree_alloc) noexcept {
    if (active_chunk == 0) {
      return std::numeric_limits<uint32_t>::max();
    }

    uint32_t best_chunk_index = __builtin_ctzll(active_chunk);
    BitmaskTree &tree = tree_alloc.get(best_chunk_index);
    uint32_t local_best = tree.get_best_bid();

    return (best_chunk_index << 18) | local_best;
  }
};
