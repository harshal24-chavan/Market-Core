#pragma once

#include "BitmaskTree.hpp"
#include "FlatHashTable.hpp"
#include "Order.hpp"
#include "SlabAllocator.hpp"
#include "messages.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <map>

uint8_t Buy = 0;
uint8_t Ask = 1;

struct PriceLevel {

  // not needed as we are using price as the array index
  // uint32_t price;

  // uint32_t total_volume{0};

  uint32_t head_index{NULL_INDEX};
  uint32_t tail_index{NULL_INDEX};
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

  // std::greater keeps the highest bids at the beginning (begin() = Best Bid)
  std::map<uint32_t, PriceLevel, std::greater<uint32_t>> sparse_bids;

  // Default keeps the lowest asks at the beginning (begin() = Best
  // Ask)
  std::map<uint32_t, PriceLevel> sparse_asks;

  static constexpr uint32_t MAX_PRICES = 1U << 27; // 2^25

  PriceLevel &get_price_level(uint8_t side, uint32_t price) {
    if (price < MAX_PRICES) {
      // THE HOT PATH: O(1) Array Lookup
      return (side == 0) ? bid_levels[price] : ask_levels[price];
    } else {
      // THE COLD PATH: O(log N) Tree Lookup for Outliers
      return (side == 0) ? sparse_bids[price] : sparse_asks[price];
    }
  }

  void append_order(PriceLevel &level, uint32_t order_index) noexcept {
    Order &newOrder = order_pool.get(order_index);

    if (level.head_index == NULL_INDEX) {
      // empty
      level.head_index = order_index;
      level.tail_index = order_index;

      newOrder.next_index = NULL_INDEX;
      newOrder.prev_index = NULL_INDEX;

      if (newOrder.price < MAX_PRICES) {
        auto &bitmaskTree = !newOrder.side ? bid_bitmask : ask_bitmask;
        bitmaskTree.set_active(newOrder.price);
      }

    } else {

      Order &oldOrder = order_pool.get(level.tail_index);
      oldOrder.next_index = order_index;

      newOrder.prev_index = level.tail_index;
      newOrder.next_index = NULL_INDEX;

      level.tail_index = order_index;
    }
    // level.total_volume += newOrder.shares;
  }

  void remove_order(PriceLevel &level, uint32_t order_index) noexcept {
    Order &order = order_pool.get(order_index);

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

    if (level.head_index == NULL_INDEX) {
      if (order.price < MAX_PRICES) {
        auto &bitmaskTree = order.side == Buy ? bid_bitmask : ask_bitmask;
        bitmaskTree.clear_active(order.price);
      }
    }

    order.next_index = NULL_INDEX;
    order.prev_index = NULL_INDEX;
  }

  void execute_order(PriceLevel &level, uint32_t order_index,
                     uint32_t executed_shares) noexcept {
    Order &order = order_pool.get(order_index);

    assert(executed_shares <= order.shares);

    order.shares -= executed_shares;
    // level.total_volume -= executed_shares;

    if (order.shares == 0) {
      remove_order(level, order_index);
    }
  }

public:
  LimitOrderbook()
      : order_map(26), bid_levels(1u << 27), ask_levels(1u << 27) {}

  void process_add(uint64_t order_id, uint32_t price, uint32_t shares,
                   uint8_t side) noexcept {
    uint32_t order_index = order_pool.allocate();

    Order &order = order_pool.get(order_index);
    order.orderRefNumber = order_id;
    order.price = price;
    order.shares = shares;
    order.side = side;

    order_map.insert(order_id, order_index);

    PriceLevel &level = get_price_level(side, price);
    append_order(level, order_index);
  }

  void process_cancel(uint64_t order_id) noexcept {
    uint32_t order_index = order_map.get(order_id);

    if (order_index == NULL_INDEX)
      return;

    Order &order = order_pool.get(order_index);

    PriceLevel &level = get_price_level(order.side, order.price);

    remove_order(level, order_index);

    order_map.erase(order_id);
    order_pool.free(order_index);
  }

  void process_partial_cancel(uint64_t order_id,
                              uint32_t canceled_shares) noexcept {
    uint32_t order_index = order_map.get(order_id);

    if (order_index == NULL_INDEX) {
      return;
    }

    Order &order = order_pool.get(order_index);

    if (canceled_shares >= order.shares) {
      process_cancel(order_id);
      return;
    }

    order.shares -= canceled_shares;

    PriceLevel &level = get_price_level(order.side, order.price);
    // level.total_volume -= canceled_shares;
  }

  void process_execute(uint64_t order_id, uint32_t executed_shares) noexcept {
    uint32_t order_index = order_map.get(order_id);
    if (order_index == NULL_INDEX)
      return;

    Order &order = order_pool.get(order_index);
    PriceLevel &level = get_price_level(order.side, order.price);

    execute_order(level, order_index, executed_shares);

    if (order.shares == 0) {
      order_map.erase(order_id);
      order_pool.free(order_index);
    }
  }

  void match_order(AddOrder &incoming_order) {
    bool isBuy = (incoming_order.buySellIndicator == 'B');

    while (incoming_order.shares) {
      uint32_t best_price;

      if (isBuy) {
        best_price = ask_bitmask.get_best_ask();
        if (best_price > incoming_order.price)
          break;

      } else {
        best_price = bid_bitmask.get_best_bid();
        if (best_price < incoming_order.price)
          break;
      }

      // If isBuy is true, fetch side 1 (Ask). If false, fetch side 0 (Bid).
      PriceLevel &level = get_price_level(isBuy ? 1 : 0, best_price);
      uint32_t node = level.head_index;

      while (node != NULL_INDEX) {
        Order &order = order_pool.get(node);

        uint32_t executed_shares =
            std::min(incoming_order.shares, order.shares);

        auto next_index = order.next_index;
        execute_order(level, node, executed_shares);

        if (order.shares <= 0) {
          order_map.erase(order.orderRefNumber);
          order_pool.free(node);
        }
        node = next_index;

        incoming_order.shares -= executed_shares;
        if (incoming_order.shares <= 0)
          break;
      }
    }
  }

  void on_add_message(const AddOrder *msg) {
    AddOrder activeOrder = *(msg);
    // match_order(activeOrder);

    if (activeOrder.shares > 0) {
      uint64_t order_ref = activeOrder.orderRefNumber;
      uint32_t price = activeOrder.price;
      uint32_t shares = activeOrder.shares;
      uint8_t side = (activeOrder.buySellIndicator == 'B') ? 0 : 1;

      process_add(order_ref, price, shares, side);
    }
  }
  void on_delete_message(const OrderDelete *msg) {
    process_cancel(msg->orderRefNumber);
  }
  void on_execute_message(const OrderExecuted *msg) {
    process_execute(msg->orderRefNumber, msg->executedShares);
  }
  void on_replace_message(const OrderReplace *msg) {
    uint64_t og_order_id = msg->originalOrderRefNumber;
    uint32_t og_order_index = order_map.get(og_order_id);

    if (og_order_index == NULL_INDEX) {
      return;
    }

    Order &og_order = order_pool.get(og_order_index);
    uint8_t side = og_order.side;
    process_cancel(og_order_id);

    AddOrder newOrder;
    newOrder.orderRefNumber = msg->newOrderRefNumber;
    newOrder.price = msg->price;
    newOrder.shares = msg->shares;
    newOrder.buySellIndicator = (side == 0) ? 'B' : 'S';

    on_add_message(&newOrder);
  }

  void on_cancel_message(const OrderCancel *msg) {
    process_partial_cancel(msg->orderRefNumber, msg->canceledShares);
  }

  /*
   *
   *
   *
   *
   *
   *
   *
   * */
  void validate_level(const PriceLevel &level, uint8_t expected_side,
                      uint32_t expected_price) {
    // Empty State Invariant
    // if (level.total_volume == 0) {
    // assert(level.head_index == NULL_INDEX &&
    //"Empty level has non-null head pointer");
    // assert(level.tail_index == NULL_INDEX &&
    //"Empty level has non-null tail pointer");
    // return;
    //}

    uint32_t current_index = level.head_index;
    uint32_t previous_index = NULL_INDEX;
    uint32_t calculated_volume = 0;

    while (current_index != NULL_INDEX) {
      Order &order = order_pool.get(current_index);

      // Core Data Invariants
      assert(order.price == expected_price && "Price Mismatch.");
      assert(order.side == expected_side && "Side Mismatch.");
      assert(order.shares > 0 && "Zero Shares must not be in LOB.");

      // Map Integrity Invariant
      assert(current_index == order_map.get(order.orderRefNumber) &&
             "Index Mismatch.");

      // Linked-List Integrity Invariants
      assert(previous_index == order.prev_index && "Previous_index Mismatch.");

      // Accumulate volume for the final check
      calculated_volume += order.shares;

      // Move to the next node
      previous_index = current_index;
      current_index = order.next_index;
    }

    // 5. The Tail & Volume Invariants (Checked after the loop)
    assert(level.tail_index == previous_index && "Tail_index Mismatch.");
    // assert(level.total_volume == calculated_volume && "total_volume
    // Mismatch.");
  }

  void validate_invariants() {
    // 1. Get ONLY the prices that currently have orders
    auto active_bids = bid_bitmask.get_active_prices();
    auto active_asks = ask_bitmask.get_active_prices();

    // 2. Validate ONLY those specific levels
    for (uint32_t price : active_bids) {
      validate_level(bid_levels[price], 0, price);
    }

    for (uint32_t price : active_asks) {
      validate_level(ask_levels[price], 1, price);
    }
  }
  /*
   *
   *
   *
   *
   *
   *
   *
   *
   *
   *
   *
   */
};
