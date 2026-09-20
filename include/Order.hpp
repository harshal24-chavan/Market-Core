#pragma once

#include "FlatHashTable.hpp"
#include <cstdint>

enum class Side { Buy = 0, Ask = 1 };

struct alignas(16) Order {
  uint32_t shares;
  uint32_t
      price_and_side; // price 27bits and side the LSB 1 bit (0 buy & 1 ask)
  uint32_t next_index{NULL_INDEX};
  uint32_t prev_index{NULL_INDEX};
};
