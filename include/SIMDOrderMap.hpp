#pragma once

#include "FlatHashTable.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>

// struct alignas(16) HashEntry {
//   uint64_t key;
//   uint32_t value;
//   uint32_t locate_code;
// };

class SIMDOrderMap {
private:
  uint8_t *ctrl{nullptr}; // metadata array
  HashEntry *table{nullptr};

  uint32_t capacity_mask;
  uint32_t capacity;
  size_t size_{0};

  static constexpr uint8_t EMPTY = 0x80;   // 10000000
  static constexpr uint8_t DELETED = 0XFE; // 11111110

  inline uint64_t hash(uint64_t key) const noexcept {
    // key ^= key >> 33;
    // key *= 0xff51afd7ed558ccdULL;
    // key ^= key >> 33;
    // return key;

    return key * 0x9E3779B97F4A7C15ULL;
  }

  inline uint64_t getH1(uint64_t hash) const noexcept {
    return (hash & capacity_mask);
  }
  inline uint8_t getH2(uint64_t hash) const noexcept {
    return static_cast<uint8_t>((hash >> 57) & 0x7F);
  }

public:
  SIMDOrderMap(uint32_t capacity_bits = 25) {
    capacity = 1 << capacity_bits;
    capacity_mask = capacity - 1;
    table = new HashEntry[capacity]();
    ctrl = new uint8_t[capacity + 16];

    std::memset(ctrl, EMPTY, capacity + 16);
  }

  ~SIMDOrderMap() {
    delete[] table;
    delete[] ctrl;
  }

  inline void insert(uint64_t key, uint32_t val,
                     uint32_t stock_locate) noexcept {

    uint64_t mixed_hash = hash(key);

    uint64_t index = getH1(mixed_hash);
    uint8_t h2 = getH2(mixed_hash);

    while (true) {

      __m128i group = _mm_loadu_si128((const __m128i *)(&ctrl[index]));

      // Extracts the MSB of all 16 bytes.
      // A '1' bit means the slot is EMPTY or DELETED!
      uint32_t available_mask = _mm_movemask_epi8(group);

      if (available_mask) {
        uint32_t offset = __builtin_ctz(available_mask);
        uint64_t pos = (index + offset) & capacity_mask;
        table[pos] = {key, val, stock_locate};
        ctrl[pos] = h2;
        if (pos < 16)
          ctrl[capacity + pos] = h2;

        size_++;
        return;
      }

      index = (index + 16) & capacity_mask;
    }
  }

  inline HashEntry *get(uint64_t key) const noexcept {
    uint64_t mixed_hash = hash(key);

    uint64_t index = getH1(mixed_hash);
    uint8_t h2 = getH2(mixed_hash);

    __m128i target = _mm_set1_epi8(h2);

    __m128i empty_target = _mm_set1_epi8(EMPTY);

    while (true) {
      __m128i group = _mm_loadu_si128((const __m128i *)(&ctrl[index]));

      __m128i match = _mm_cmpeq_epi8(target, group);

      uint32_t mask = _mm_movemask_epi8(match);

      while (mask) {
        uint32_t offset = __builtin_ctz(mask); // Get the lowest 1 bit

        uint64_t pos = (index + offset) & capacity_mask;
        if (table[pos].key == key) {
          return &table[pos];
        }

        mask &= (mask - 1); // unset the bit
      }

      __m128i empty_match = _mm_cmpeq_epi8(empty_target, group);
      uint32_t empty_mask = _mm_movemask_epi8(empty_match);

      if (empty_mask) // we found an empty spot
        return nullptr;

      index = (index + 16) & capacity_mask;
    }
  }

  inline void erase(uint64_t key) noexcept {
    deleteCount++;
    uint64_t mixed_hash = hash(key);

    uint64_t index = getH1(mixed_hash);
    uint8_t h2 = getH2(mixed_hash);

    __m128i target = _mm_set1_epi8(h2);

    __m128i empty_target = _mm_set1_epi8(EMPTY);

    while (true) {
      __m128i group = _mm_loadu_si128((__m128i *)(&ctrl[index]));

      __m128i match = _mm_cmpeq_epi8(target, group);

      uint32_t mask = _mm_movemask_epi8(match);

      while (mask) {
        uint32_t offset = __builtin_ctz(mask);
        uint64_t pos = (index + offset) & capacity_mask;
        if (table[pos].key == key) {
          ctrl[pos] = DELETED;
          if (pos < 16)
            ctrl[capacity + pos] = DELETED;
          size_--;
          return;
        }
        mask &= (mask - 1);
      }

      __m128i empty_match = _mm_cmpeq_epi8(empty_target, group);
      uint32_t empty_mask = _mm_movemask_epi8(empty_match);
      if (empty_mask)
        return;

      index = (index + 16) & capacity_mask;
    }
  }

  uint32_t size() const { return size_; }
};
