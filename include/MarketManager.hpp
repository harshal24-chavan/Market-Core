#pragma once

#include "BitmaskTree.hpp"
#include "FlatHashTable.hpp"
#include "Order.hpp"
#include "PageAllocator.hpp"
#include "SIMDOrderMap.hpp"
#include "SingleStockBook.hpp"
#include "SlabAllocator.hpp"
#include "messages.hpp"
#include <atomic>
#include <cassert>

inline uint32_t to_dense_index(uint32_t itch_price) noexcept {
  // Sub-dollar prices ($0.0000 to $0.9999) keep 0.0001 granularity
  if (itch_price < 10000) {
    return itch_price;
  }
  // Prices >= $1.00 are scaled to 0.01 granularity (cents)
  // Offset by 10000 so they seamlessly continue after the sub-dollar range
  return 10000 + ((itch_price - 10000) / 100);
}

std::atomic<uint64_t> drop_count{0};
std::uint32_t PRICE_LIMIT = 2097152;

class MarketManager {
private:
  using TreeAllocator = SlabAllocator<BitmaskTree, 15>;

  SlabAllocator<Order> global_pool;
  // OrderMap global_map;
  SIMDOrderMap global_map;
  PageAllocator global_pages;
  SingleStockBook stock_books[10000];
  TreeAllocator global_trees;

public:
  void process_add(const AddOrder *msg, uint16_t locate_code) noexcept {
    uint32_t price = bswap32(msg->price);
    uint32_t dense_price = to_dense_index(price);
    uint8_t side = (msg->buySellIndicator == 'B') ? 0 : 1;

    // Graceful drop instead of assert
    if (__builtin_expect(dense_price >= PRICE_LIMIT, 0)) {
      drop_count++;
      return;
    }

    uint32_t order_index = global_pool.allocate();
    if (order_index == NULL_INDEX) {
      assert(false && "cannot add, map full.");
    }

    uint64_t order_id = bswap64(msg->orderRefNumber);
    uint32_t shares = bswap32(msg->shares);
    global_map.insert(order_id, order_index, locate_code);

    stock_books[locate_code].add_order(order_index, dense_price, shares, side,
                                       global_pool, global_pages, global_trees);
  }

  void process_delete(uint64_t order_id) noexcept {
    auto *slot = global_map.get(order_id);
    if (!slot)
      return;

    uint32_t order_index = slot->value;
    uint32_t locate_code = slot->locate_code;

    __builtin_prefetch(&global_pool.get(order_index), 1, 3);
    stock_books[locate_code].cancel_order(order_index, global_pool,
                                          global_pages, global_trees);

    global_map.erase(order_id);
  }

  void process_execute(uint64_t order_id, uint32_t executed_shares) noexcept {
    auto *slot = global_map.get(order_id);
    if (!slot)
      return;

    uint32_t order_index = slot->value;
    uint32_t locate_code = slot->locate_code;

    assert(locate_code < 10000 && "FATAL: Locate code is out of bounds!");
    assert(order_index != NULL_INDEX &&
           "FATAL: Hash Table returned a dead slab index!");
    bool isDead = stock_books[locate_code].execute_order(
        order_index, executed_shares, global_pool, global_pages, global_trees);

    if (isDead)
      global_map.erase(order_id);
  }

  void process_cancel(uint64_t order_id, uint32_t canceled_shares) noexcept {
    auto *slot = global_map.get(order_id);
    if (!slot) {
      return;
    }

    uint32_t order_index = slot->value;
    uint32_t locate_code = slot->locate_code;

    bool isDead = stock_books[locate_code].partial_cancel_order(
        order_index, canceled_shares, global_pool, global_pages, global_trees);

    if (isDead)
      global_map.erase(order_id);
  }

  void process_replace(const OrderReplace *msg) noexcept {
    uint64_t old_id = bswap64(msg->originalOrderRefNumber);

    auto *slot = global_map.get(old_id);
    if (!slot) {
      return;
    }

    uint32_t order_index = slot->value;
    uint32_t locate_code = slot->locate_code;

    Order &order = global_pool.get(order_index);
    uint8_t side = order.price_and_side & 1;

    stock_books[locate_code].cancel_order(order_index, global_pool,
                                          global_pages, global_trees);
    global_map.erase(old_id);

    uint64_t new_id = bswap64(msg->newOrderRefNumber);
    uint32_t new_price = bswap32(msg->price);
    uint32_t new_shares = bswap32(msg->shares);

    uint32_t new_index = global_pool.allocate();

    global_map.insert(new_id, new_index, locate_code);

    uint32_t dense_price = to_dense_index(new_price);

    // TEMP FIX: update to use flatmap later
    if (__builtin_expect(dense_price >= PRICE_LIMIT, 0)) {
      drop_count++;
      return;
    }
    stock_books[locate_code].add_order(new_index, dense_price, new_shares, side,
                                       global_pool, global_pages, global_trees);
  }
};
