#pragma once

#include <cstdint>
#include <limits>

class BitmaskTree {
private:
  static constexpr uint32_t MAX_PRICES = 1U << 18;

  // 1<<18 amount of bits are present in this level
  uint64_t bottom[4096]{0};

  // 1<<6 bits present i.e 4096 to represent bottom level
  uint64_t middle[64]{0};

  // 1 word ie 64 bits to point to middle level
  uint64_t root{0};

public:
  BitmaskTree() = default;

  void set_active(uint32_t price) {

    auto bottom_word = price >> 6;
    auto bottom_bit = price & 63;
    bottom[bottom_word] |= (1Ull << bottom_bit);

    auto middle_word = bottom_word >> 6;
    auto middle_bit = bottom_word & 63;
    middle[middle_word] |= (1ULL << middle_bit);

    root |= (1ULL << middle_word);
  }

  void clear_active(uint32_t price) {

    auto bottom_word = price >> 6;
    auto bottom_bit = price & 63;
    bottom[bottom_word] &= ~(1ULL << bottom_bit);

    if (bottom[bottom_word] != 0)
      return;

    auto middle_word = bottom_word >> 6;
    auto middle_bit = bottom_word & 63;
    middle[middle_word] &= ~(1ULL << middle_bit);

    if (middle[middle_word] != 0)
      return;

    root &= ~(1ULL << middle_word);
  }

  uint32_t get_best_bid() {

    if (!root)
      return 0;

    auto root_bit = 63 - __builtin_clzll(root);

    auto middle_word = middle[root_bit];
    auto middle_bit = 63 - __builtin_clzll(middle_word);

    auto bottom_word_index = (root_bit << 6) + middle_bit;
    auto bottom_word = bottom[bottom_word_index];
    auto bottom_bit = 63 - __builtin_clzll(bottom_word);

    return (bottom_word_index << 6) + bottom_bit;
  }

  uint32_t get_best_ask() {

    if (!root)
      return std::numeric_limits<uint32_t>::max();

    auto root_bit = __builtin_ctzll(root);

    auto middle_word = middle[root_bit];
    auto middle_bit = __builtin_ctzll(middle_word);

    auto bottom_word_index = (root_bit << 6) + middle_bit;
    auto bottom_word = bottom[bottom_word_index];
    auto bottom_bit = __builtin_ctzll(bottom_word);

    return (bottom_word_index << 6) + bottom_bit;
  }

  bool isEmpty() noexcept { return root == 0; }

  //  std::vector<uint32_t> get_active_prices() const {
  //    std::vector<uint32_t> active_prices;
  //    // Pre-allocate a reasonable size to avoid vector resizing overhead
  //    during
  //    // testing
  //    active_prices.reserve(256);
  //
  //    uint64_t l4_mask = level4[0];
  //
  //    // Level 4
  //    while (l4_mask != 0) {
  //      uint32_t l4_bit = __builtin_ctzll(l4_mask);
  //
  //      uint32_t l3_word_index = l4_bit;
  //      uint64_t l3_mask = level3[l3_word_index];
  //
  //      // Level 3
  //      while (l3_mask != 0) {
  //        uint32_t l3_bit = __builtin_ctzll(l3_mask);
  //
  //        uint32_t l2_word_index = (l3_word_index << 6) + l3_bit;
  //        uint64_t l2_mask = level2[l2_word_index];
  //
  //        // Level 2
  //        while (l2_mask != 0) {
  //          uint32_t l2_bit = __builtin_ctzll(l2_mask);
  //
  //          uint32_t l1_word_index = (l2_word_index << 6) + l2_bit;
  //          uint64_t l1_mask = level1[l1_word_index];
  //
  //          // Level 1
  //          while (l1_mask != 0) {
  //            uint32_t l1_bit = __builtin_ctzll(l1_mask);
  //
  //            uint32_t l0_word_index = (l1_word_index << 6) + l1_bit;
  //            uint64_t l0_mask = level0[l0_word_index];
  //
  //            // Level 0 (The actual prices)
  //            while (l0_mask != 0) {
  //              uint32_t l0_bit = __builtin_ctzll(l0_mask);
  //
  //              uint32_t price = (l0_word_index << 6) + l0_bit;
  //              active_prices.push_back(price);
  //
  //              // Clear the lowest set bit to move to the next one
  //              l0_mask &= (l0_mask - 1);
  //            }
  //            l1_mask &= (l1_mask - 1);
  //          }
  //          l2_mask &= (l2_mask - 1);
  //        }
  //        l3_mask &= (l3_mask - 1);
  //      }
  //      l4_mask &= (l4_mask - 1);
  //    }
  //
  //    return active_prices;
  //  }
};
