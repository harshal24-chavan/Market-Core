#pragma once

#include "FlatHashTable.hpp"
#include <cstdint>
#include <limits>
#include <vector>

enum class Side { Buy = 0, Ask = 1 };

struct Order {
  uint64_t orderRefNumber;
  uint32_t shares;
  uint32_t price;
  uint32_t next_index{NULL_INDEX};
  uint32_t prev_index{NULL_INDEX};
  uint8_t side; // buy = 0, ask = 1
};

class OrderPool {
private:
  std::vector<Order> pool;
  std::vector<uint32_t> free_indices;

  uint32_t top{MAX_SIZE - 1};
  static constexpr uint32_t MAX_SIZE = 1000000;

public:
  OrderPool() noexcept {
    pool.resize(MAX_SIZE);
    free_indices.resize(MAX_SIZE);
    for (int i = 0; i < MAX_SIZE; i++) {
      free_indices[i] = i;
    }
  }

  /* gives free index where we can store orders*/
  uint32_t allocate() noexcept {
    uint32_t res{0};
    if (top == NULL_INDEX) {
      return NULL_INDEX;
    }

    res = free_indices[top];
    top--;
    return res;
  }

  /* returns the index to the pool for reuse*/
  void deallocate(uint32_t index) noexcept {
    if (top + 1 >= MAX_SIZE) {
      return;
    }

    top++;
    free_indices[top] = index;
  }

  Order &getOrder(uint32_t index) noexcept { return pool[index]; }
};
