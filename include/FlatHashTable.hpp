#pragma once
#include <algorithm>
#include <cstdint>

constexpr uint64_t EMPTY_SLOT = 0;
constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;

/*
 * Implementing Robin hood hashing + backwards shift deletion
 *
 * for explanation please visit blog by Emmanuel Goossaert:
 * https://codecapsule.com/2013/11/11/robin-hood-hashing/
 *
 * and another blog:
 * https://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
 */

struct alignas(16) HashEntry {
  uint64_t key;             // will hold the order reference number
  uint32_t value;           // will hold the slab allocators index
  uint32_t dib{NULL_INDEX}; // will hold Distance from initial bucket

  inline bool empty() const noexcept { return key == EMPTY_SLOT; }
};

class OrderMap {
private:
  uint32_t capacity_mask;
  uint32_t capacity;
  HashEntry *table;
  uint32_t size_{0};

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
    HashEntry currentEntry{key, val, 0};
    uint32_t hashIndex = hash(key);
    while (true) {
      HashEntry &entry = table[hashIndex];
      if (entry.empty()) {
        // bucket empty
        entry = currentEntry;
        return;
      }

      if (entry.dib < currentEntry.dib) {
        // swap the bucket
        std::swap(entry, currentEntry);
      }

      ++currentEntry.dib;
      hashIndex = (hashIndex + 1) & capacity_mask;
    }
    size_++;
  }

  inline uint32_t get(uint64_t key) const noexcept {
    uint32_t ind = hash(key);
    uint32_t currentDib = 0;

    while (true) {
      const HashEntry &entry = table[ind];
      if (entry.empty()) {
        return NULL_INDEX;
      }
      if (entry.dib < currentDib) {
        return NULL_INDEX;
      }
      if (entry.key == key) {
        return entry.value;
      }

      ind = (ind + 1) & capacity_mask;
      currentDib++;
    }
    // will not reach here
    return NULL_INDEX;
  }

  inline void erase(uint64_t key) {
    uint32_t hashIndex = hash(key);
    uint32_t currentDib = 0;

    while (true) {
      HashEntry &entry = table[hashIndex];

      if (entry.empty()) {
        return;
      }

      if (entry.dib < currentDib) {
        return;
      }

      if (entry.key == key) {
        // we found the entry to delete
        break;
      }

      hashIndex = (hashIndex + 1) & capacity_mask;
      ++currentDib;
    }

    uint32_t hole = hashIndex;
    uint32_t next = (hole + 1) & capacity_mask;

    while (true) {
      HashEntry &nextEntry = table[next];
      if (nextEntry.empty() || nextEntry.dib == 0) {
        table[hole].key = EMPTY_SLOT;
        table[hole].value = EMPTY_SLOT;
        table[hole].dib = NULL_INDEX;

        size_--;
        return;
      }

      table[hole] = nextEntry;
      table[hole].dib--;

      hole = next;
      next = (next + 1) & capacity_mask;
    }
  }

  uint32_t size() { return size_; }
};
