#pragma once

#include <cstdint>

struct PriceLevel {
  uint32_t price;

  uint32_t total_volume{0};

  uint32_t head_index{0xFFFFFFFF};
  uint32_t tail_index{0xFFFFFFFF};
};
