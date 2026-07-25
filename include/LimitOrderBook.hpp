#pragma once

#include "Order.hpp"
#include <cstdint>

constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;

struct PriceLevel {
  uint32_t price;

  uint32_t total_volume{0};

  uint32_t head_index{0xFFFFFFFF};
  uint32_t tail_index{0xFFFFFFFF};
};

class LimitOrderbook {
private:
  OrderPool pool;
  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;

public:
  LimitOrderbook() {
    bids.resize(1000000);
    asks.resize(1000000);

    for (uint32_t i = 0; i < 1000000; i++) {
      bids[i].price = i;
      asks[i].price = i;
    }
  }

  void add_order(char side, uint32_t price, uint32_t shares,
                 uint64_t orderRef) {
    uint32_t index = pool.allocate();
    if (index == NULL_INDEX) {
      return;
    }

    Order &order = pool.getOrder(index);
    order.orderRefNumber = orderRef;
    order.price = price;
    order.shares = shares;

    PriceLevel &p = (side == 'B') ? bids[price] : asks[price];
    p.total_volume += shares;

    if (p.head_index == NULL_INDEX) {
      p.head_index = index;
      p.tail_index = index;
    } else {
      uint32_t tailIndex = p.tail_index;

      Order tailOrder = pool.getOrder(tailIndex);
      tailOrder.next_index = index;
      p.tail_index = index;

      order.prev_index = tailIndex;
    }
  }
};
