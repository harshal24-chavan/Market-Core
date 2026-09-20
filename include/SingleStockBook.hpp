#pragma once

#include "BitmaskTree.hpp"
#include "FlatHashTable.hpp"
#include "Order.hpp"
#include "PageAllocator.hpp"
#include "SlabAllocator.hpp"

class SingleStockBook {
private:
  PagedPriceArray bid_levels;
  PagedPriceArray ask_levels;

  BitmaskTree bid_tree;
  BitmaskTree ask_tree;

  static constexpr uint8_t BUY = 0;
  static constexpr uint8_t ASK = 1;

  inline void append_order(PriceLevel &level, uint32_t order_index,
                           Order &newOrder,
                           SlabAllocator<Order> &pool) noexcept {
    if (level.head_index == NULL_INDEX) {
      // setting the price levels (intrusive list)
      level.head_index = order_index;
      level.tail_index = order_index;

      newOrder.next_index = NULL_INDEX;
      newOrder.prev_index = NULL_INDEX;

      // setting the bitmaskTree
      uint32_t price = newOrder.price_and_side >> 1;
      uint8_t side = newOrder.price_and_side & 1;

      if (side == BUY)
        bid_tree.set_active(price);
      else
        ask_tree.set_active(price);
    } else {
      // orders already  present
      Order &oldTail = pool.get(level.tail_index);
      oldTail.next_index = order_index;

      newOrder.prev_index = level.tail_index;
      newOrder.next_index = NULL_INDEX;

      level.tail_index = order_index;
    }

    level.total_volume += newOrder.shares;
    level.order_count++;
  }

  inline void remove_order(PriceLevel &level, uint32_t order_index,
                           Order &order, SlabAllocator<Order> &pool) noexcept {

    if (order.prev_index != NULL_INDEX) {
      pool.get(order.prev_index).next_index = order.next_index;
    } else {
      level.head_index = order.next_index;
    }

    if (order.next_index != NULL_INDEX) {
      pool.get(order.next_index).prev_index = order.prev_index;
    } else {
      level.tail_index = order.prev_index;
    }

    level.total_volume -= order.shares;
    level.order_count--;

    if (level.order_count == 0) {
      uint32_t price = order.price_and_side >> 1;
      uint8_t side = order.price_and_side & 1;

      if (side == BUY)
        bid_tree.clear_active(price);
      else
        ask_tree.clear_active(price);
    }

    order.next_index = NULL_INDEX;
    order.prev_index = NULL_INDEX;
  }

public:
  inline void add_order(uint32_t order_index, uint32_t price, uint32_t shares,
                        uint8_t side, SlabAllocator<Order> &order_pool,
                        PageAllocator &page_alloc) noexcept {
    assert(price < 2097152 && "FATAL: Dense price exceeds Array/Tree bounds!");

    Order &order = order_pool.get(order_index);
    order.shares = shares;
    order.price_and_side = (price << 1) + side;

    PriceLevel &level = (side == BUY) ? bid_levels.get_level(price, page_alloc)
                                      : ask_levels.get_level(price, page_alloc);

    append_order(level, order_index, order, order_pool);
  }

  inline void cancel_order(uint32_t order_index, SlabAllocator<Order> &pool,
                           PageAllocator &page_alloc) {
    Order &order = pool.get(order_index);
    uint32_t price = order.price_and_side >> 1;
    uint8_t side = order.price_and_side & 1;

    PriceLevel &level = side == BUY ? bid_levels.get_level(price, page_alloc)
                                    : ask_levels.get_level(price, page_alloc);

    remove_order(level, order_index, order, pool);
    pool.free(order_index);
  }

  inline bool execute_order(uint32_t order_index, uint32_t executed_shares,
                            SlabAllocator<Order> &pool,
                            PageAllocator &page_alloc) {
    Order &order = pool.get(order_index);
    uint32_t price = order.price_and_side >> 1;
    uint8_t side = order.price_and_side & 1;

    PriceLevel &level = side == BUY ? bid_levels.get_level(price, page_alloc)
                                    : ask_levels.get_level(price, page_alloc);

    if (__builtin_expect(executed_shares >= order.shares, 0)) {
      executed_shares = order.shares;
    }

    order.shares -= executed_shares;
    level.total_volume -= executed_shares;

    if (order.shares == 0) {
      remove_order(level, order_index, order, pool);
      pool.free(order_index);
      return true;
    }

    return false;
  }

  inline bool partial_cancel_order(uint32_t order_index,
                                   uint32_t canceled_shares,
                                   SlabAllocator<Order> &pool,
                                   PageAllocator &page_alloc) noexcept {

    Order &order = pool.get(order_index);
    if (__builtin_expect(canceled_shares >= order.shares, 0)) {
      canceled_shares = order.shares;
    }

    if (canceled_shares >= order.shares) {
      cancel_order(order_index, pool, page_alloc);
      return true;
    }

    uint32_t price = order.price_and_side >> 1;
    uint8_t side = order.price_and_side & 1;

    PriceLevel &level = side == BUY ? bid_levels.get_level(price, page_alloc)
                                    : ask_levels.get_level(price, page_alloc);

    order.shares -= canceled_shares;
    level.total_volume -= canceled_shares;
    return false;
  }
};
