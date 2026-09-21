#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

constexpr uint64_t EMPTY_SLOT = 0;
constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;
constexpr uint32_t MAX_PROBES = 12000;

uint64_t deleteCount{0};
uint64_t actualDeleteCount{0};

struct alignas(16) HashEntry {
  uint64_t key;
  uint32_t value;
  uint32_t locate_code;
};

class OrderMap {
private:
  uint32_t capacity_mask;
  uint32_t capacity;
  HashEntry *table;
  uint32_t size_{0};

public:
  OrderMap(uint32_t capacity_bits = 27) {
    capacity = 1 << capacity_bits;
    capacity_mask = capacity - 1;
    table = new HashEntry[capacity]();
  }

  ~OrderMap() { delete[] table; }

  inline uint32_t hash(uint64_t key) const noexcept {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    return key & capacity_mask;
  }

  inline void insert(uint64_t key, uint32_t val,
                     uint32_t stock_locate) noexcept {
    uint32_t ind = hash(key);
    uint32_t probes = 0;

    while (true) {
      if (table[ind].key == EMPTY_SLOT) {
        table[ind].key = key;
        table[ind].value = val;
        table[ind].locate_code = stock_locate;
        size_++;
        return;
      }

      if (table[ind].key == key) {
        table[ind].value = val;
        table[ind].locate_code = stock_locate;
        return;
      }

      ind = (ind + 1) & capacity_mask;

      if (__builtin_expect(++probes > MAX_PROBES, 0)) {
        fprintf(
            stderr,
            "\nFATAL: Hit MAX_PROBES insert! Table Size: %u (%.1f%% full)\n",
            size_, (float)size_ / capacity * 100.0f);
        exit(1);
      }
    }
  }

  inline HashEntry *get(uint64_t key) const noexcept {
    uint32_t ind = hash(key);
    uint32_t probes = 0;

    while (table[ind].key != EMPTY_SLOT) {
      if (table[ind].key == key) {
        return &table[ind];
      }
      ind = (ind + 1) & capacity_mask;

      if (__builtin_expect(++probes > MAX_PROBES, 0)) {
        fprintf(stderr,
                "\nFATAL: Hit MAX_PROBES get! Table Size: %u (%.1f%% full)\n",
                size_, (float)size_ / capacity * 100.0f);
        exit(1);
      }
    }
    return nullptr;
  }

  inline void erase(uint64_t key) noexcept {
    uint32_t i = hash(key);
    uint32_t probes = 0;

    deleteCount++;

    while (table[i].key != EMPTY_SLOT) {
      if (table[i].key == key) {
        // Create the hole
        table[i].key = EMPTY_SLOT;
        size_--;

        // Backward Shift
        uint32_t j = i;
        while (true) {
          j = (j + 1) & capacity_mask;

          // If we hit an empty slot, the collision cluster is over.
          if (table[j].key == EMPTY_SLOT) {
            break;
          }

          // Where does the element at j ACTUALLY want to be?
          uint32_t ideal_bucket = hash(table[j].key);

          // Is the hole 'i' on the natural probe path between ideal_bucket and
          // 'j'? By using unsigned arithmetic & mask, this perfectly handles
          // array wrap-around!
          if (((i - ideal_bucket) & capacity_mask) <
              ((j - ideal_bucket) & capacity_mask)) {
            // Move the element backward into the hole
            table[i] = table[j];
            table[j].key = EMPTY_SLOT;
            i = j; // The hole has now moved to j
          }
        }
        actualDeleteCount++;
        return;
      }
      i = (i + 1) & capacity_mask;

      if (__builtin_expect(++probes > MAX_PROBES, 0)) {
        fprintf(stderr,
                "\nFATAL: Hit MAX_PROBES erase! Table Size: %u (%.1f%% full)\n",
                size_, (float)size_ / capacity * 100.0f);
        exit(1);
      }
    }
  }

  uint32_t size() const { return size_; }
};
