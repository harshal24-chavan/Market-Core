#pragma once

#include "BitmaskTree.hpp"
#include "FlatHashTable.hpp"
#include "Order.hpp"
#include "SlabAllocator.hpp"
#include <cstdint>

struct PriceLevel {
  uint32_t price;

  uint32_t total_volume{0};

  uint32_t head_index{0xFFFFFFFF};
  uint32_t tail_index{0xFFFFFFFF};
};

class LimitOrderbook {
private:
  SlabAllocator<Order> order_pool;
  OrderMap order_map;

  // Flat Arrays of Price Levels (Index = Exact Price)
  std::vector<PriceLevel> bid_levels;
  std::vector<PriceLevel> ask_levels;

  BitmaskTree bid_bitmask; // to get best bid in O(1)
  BitmaskTree ask_bitmask; // to get best ask in O(1)

  void append_order(PriceLevel &level, uint32_t order_index) {
    Order &newOrder = order_pool.get(order_index);

    if (level.head_index == NULL_INDEX) {
      // empty
      level.head_index = order_index;
      level.tail_index = order_index;

      newOrder.next_index = NULL_INDEX;
      newOrder.prev_index = NULL_INDEX;

      auto &bitmaskTree = !newOrder.side ? bid_bitmask : ask_bitmask;
      bitmaskTree.set_active(newOrder.price);

    } else {

      Order &oldOrder = order_pool.get(level.tail_index);
      oldOrder.next_index = order_index;

      newOrder.prev_index = level.tail_index;
      newOrder.next_index = NULL_INDEX;

      level.tail_index = order_index;
    }
    level.total_volume += newOrder.shares;
  }

  void remove_order(PriceLevel &level, uint32_t order_index) {
    Order &order = order_pool.get(order_index);
    level.total_volume -= order.shares;

    if (!level.total_volume) {
      auto &bitmaskTree = !order.side ? bid_bitmask : ask_bitmask;
      bitmaskTree.clear_active(order.price);
    }

    if (order.prev_index != NULL_INDEX) {
      // We have a left neighbor. Connect it to our right neighbor.
      order_pool.get(order.prev_index).next_index = order.next_index;
    } else {
      // We have no left neighbor. We must be the head.
      level.head_index = order.next_index;
    }

    if (order.next_index != NULL_INDEX) {
      // We have a right neighbor. Connect it to our left neighbor.
      order_pool.get(order.next_index).prev_index = order.prev_index;
    } else {
      // We have no right neighbor. We must be the tail.
      level.tail_index = order.prev_index;
    }
  }

  void execute_order(PriceLevel &level, uint32_t order_index,
                     uint32_t executed_shares) {
    Order &order = order_pool.get(order_index);
    if (executed_shares >= order.shares) {
      remove_order(level, order_index);

      order_map.erase(order.orderRefNumber);
    } else {
      order.shares -= executed_shares;
      level.total_volume -= executed_shares;
    }
  }

public:
  LimitOrderbook() : order_map(26) {}

  void process_add(uint64_t order_id, uint32_t price, uint32_t shares,
                   uint8_t side) noexcept {
    uint32_t order_index = order_pool.allocate();

    Order &order = order_pool.get(order_index);
    order.orderRefNumber = order_id;
    order.price = price;
    order.shares = shares;
    order.side = side;

    order_map.insert(order_id, order_index);

    PriceLevel &level = !side ? bid_levels[price] : ask_levels[price];
    append_order(level, order_index);
  }

  void process_cancel(uint64_t order_id) noexcept {
    uint32_t order_index = order_map.get(order_id);

    if (order_index == NULL_INDEX)
      return;

    Order &order = order_pool.get(order_index);

    PriceLevel &level =
        !order.side ? bid_levels[order.price] : ask_levels[order.price];

    remove_order(level, order_index);

    order_map.erase(order_id);
    order_pool.free(order_index);
  }

  void process_execute(uint64_t order_id, uint32_t executed_shares) noexcept {
    uint32_t order_index = order_map.get(order_id);
    if (order_index == NULL_INDEX)
      return;

    Order &order = order_pool.get(order_index);
    PriceLevel &level =
        !order.side ? bid_levels[order.price] : ask_levels[order.price];

    execute_order(level, order_index, executed_shares);

    if (order.shares == 0) {
      order_map.erase(order_id);
      order_pool.free(order_index);
    }
  }
};
