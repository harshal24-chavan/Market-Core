#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

constexpr uint64_t EMPTY_SLOT = 0;
constexpr uint64_t TOMBSTONE = std::numeric_limits<uint64_t>::max();
constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;

struct alignas(16) HashEntry {
  uint64_t key;   // will hold the order reference number
  uint32_t value; // will hold the slab allocators index
};

class OrderMap {
private:
  uint32_t capacity_mask;
  uint32_t capacity;
  HashEntry *table;
  uint32_t size_;

public:
  OrderMap(uint32_t capacity_bits = 21) {
    capacity = 1 << capacity_bits;
    capacity_mask = capacity - 1;

    table = new HashEntry[capacity]();
  }

  ~OrderMap() { delete[] table; }

  inline uint32_t hash(uint64_t key) const {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    return key & capacity_mask;
  }

  inline void insert(uint64_t key, uint32_t val) noexcept {
    uint32_t ind = hash(key);

    uint32_t probe = 1;

    while (true) {
      probe++;
      if (table[ind].key == EMPTY_SLOT || table[ind].key == TOMBSTONE) {
        table[ind].key = key;
        table[ind].value = val;
        size_++;

        return;
      }
      ind = (ind + 1) & capacity_mask;
    }
  }

  inline uint32_t get(uint64_t key) const noexcept {
    uint32_t ind = hash(key);
    while (table[ind].key != EMPTY_SLOT) {
      if (table[ind].key == key) {
        return table[ind].value;
      }
      ind = (ind + 1) & capacity_mask;
    }
    return NULL_INDEX;
  }

  inline void erase(uint64_t key) {
    uint32_t ind = hash(key);

    while (table[ind].key != EMPTY_SLOT) {
      if (table[ind].key == key) {
        table[ind].key = TOMBSTONE;
        table[ind].value = 0;
        size_--;
        return;
      }
      ind = (ind + 1) & capacity_mask;
    }
    return;
  }

  uint32_t size() { return size_; }
};
