#pragma once

#include <cstdint>
#include <limits>

static constexpr uint32_t words_needed(uint32_t bits) noexcept {
  return (bits + 63) / 64;
}

class BitmaskTree {
private:
  static constexpr uint32_t MAX_PRICES = 33554432; // 2^25

  static constexpr uint32_t L0_SIZE = words_needed(MAX_PRICES);
  static constexpr uint32_t L1_SIZE = words_needed(L0_SIZE);
  static constexpr uint32_t L2_SIZE = words_needed(L1_SIZE);
  static constexpr uint32_t L3_SIZE = words_needed(L2_SIZE);
  static constexpr uint32_t L4_SIZE = words_needed(L3_SIZE);

  uint64_t level0[L0_SIZE]{0};
  uint64_t level1[L1_SIZE]{0};
  uint64_t level2[L2_SIZE]{0};
  uint64_t level3[L3_SIZE]{0};
  uint64_t level4[L4_SIZE]{0};

public:
  BitmaskTree() = default;

  void set_active(uint32_t price) {
    auto l0_word = price >> 6;
    auto l0_bit = price & 63;

    level0[l0_word] |= (1ULL << l0_bit);

    auto l1_word = l0_word >> 6;
    auto l1_bit = l0_word & 63;
    level1[l1_word] |= (1ULL << l1_bit);

    auto l2_word = l1_word >> 6;
    auto l2_bit = l1_word & 63;
    level2[l2_word] |= (1ULL << l2_bit);

    auto l3_word = l2_word >> 6;
    auto l3_bit = l2_word & 63;
    level3[l3_word] |= (1ULL << l3_bit);

    auto l4_word = 0;
    auto l4_bit = l3_word & 63;
    level4[l4_word] |= (1ULL << l4_bit);
  }

  void clear_active(uint32_t price) {
    auto l0_word = price >> 6;
    auto l0_bit = price & 63;

    // Clear the bit at level 0
    level0[l0_word] &= ~(1ULL << l0_bit);

    // If the level 0 word still has other bits set, we STOP.
    // We do not clear the levels above.
    if (level0[l0_word] != 0)
      return;

    auto l1_word = l0_word >> 6;
    auto l1_bit = l0_word & 63;
    level1[l1_word] &= ~(1ULL << l1_bit);

    if (level1[l1_word] != 0)
      return;

    auto l2_word = l1_word >> 6;
    auto l2_bit = l1_word & 63;
    level2[l2_word] &= ~(1ULL << l2_bit);

    if (level2[l2_word] != 0)
      return;

    auto l3_word = l2_word >> 6;
    auto l3_bit = l2_word & 63;
    level3[l3_word] &= ~(1ULL << l3_bit);

    if (level3[l3_word] != 0)
      return;

    auto l4_word = 0;
    auto l4_bit = l3_word & 63;
    level4[l4_word] &= ~(1ULL << l4_bit);
  }

  uint32_t get_best_bid() {
    auto l4_word = level4[0];

    if (!l4_word)
      return std::numeric_limits<uint32_t>::max();
    auto l4_bit = 63 - __builtin_clzll(l4_word); // getting the MSB with clzll

    auto l3_word = level3[l4_bit]; // multiply by 64
    auto l3_bit = 63 - __builtin_clzll(l3_word);

    auto l2_word_index = (l4_bit << 6) + l3_bit;
    auto l2_word = level2[l2_word_index];
    auto l2_bit = 63 - __builtin_clzll(l2_word);

    auto l1_word_index = (l2_word_index << 6) + l2_bit;
    auto l1_word = level1[l1_word_index];
    auto l1_bit = 63 - __builtin_clzll(l1_word);

    auto l0_word_index = (l1_word_index << 6) + l1_bit;
    auto l0_word = level0[l0_word_index];
    auto l0_bit = 63 - __builtin_clzll(l0_word);

    return (l0_word_index << 6) + l0_bit;
  }

  uint32_t get_best_ask() {
    auto l4_word = level4[0];

    if (!l4_word)
      return std::numeric_limits<uint32_t>::max();

    auto l4_bit = __builtin_ctzll(l4_word); // get the LSB bit set to 1

    auto l3_word_index = l4_bit;
    auto l3_word = level3[l3_word_index];
    auto l3_bit = __builtin_ctzll(l3_word);

    auto l2_word_index = (l3_word_index << 6) + l3_bit;
    auto l2_word = level2[l2_word_index];
    auto l2_bit = __builtin_ctzll(l2_word);

    auto l1_word_index = (l2_word_index << 6) + l2_bit;
    auto l1_word = level1[l1_word_index];
    auto l1_bit = __builtin_ctzll(l1_word);

    auto l0_word_index = (l1_word_index << 6) + l1_bit;
    auto l0_word = level0[l0_word_index];
    auto l0_bit = __builtin_ctzll((l0_word));

    return (l0_word_index << 6) + l0_bit;
  }
};
