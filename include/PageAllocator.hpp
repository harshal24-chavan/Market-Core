#pragma once
#include <algorithm> // Required for std::fill
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>

constexpr uint16_t NULL_SLOT = std::numeric_limits<uint16_t>::max();

struct alignas(16) PriceLevel {
  uint32_t head_index{NULL_INDEX};
  uint32_t tail_index{NULL_INDEX};
  uint32_t total_volume{0};
  uint32_t order_count{0};
};

class PageAllocator {
private:
  PriceLevel *memory_pool;
  uint32_t next_free_page{0};

public:
  explicit PageAllocator(uint32_t max_pages = 50000) {
    size_t raw_bytes = max_pages * 4096 * sizeof(PriceLevel);
    constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;
    size_t bytes = (raw_bytes + (HUGE_PAGE_SIZE - 1)) & ~(HUGE_PAGE_SIZE - 1);

    memory_pool = static_cast<PriceLevel *>(::mmap(
        nullptr, bytes, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0));

    if (memory_pool == MAP_FAILED)
      throw std::runtime_error("Page Allocator OOM");

    ::madvise(memory_pool, bytes, MADV_HUGEPAGE);

    std::cout << "PageAllocator Virtual Memory Reserved: "
              << (bytes / (1024 * 1024)) << " MB\n";
  }

  inline PriceLevel *get_page(uint32_t index) noexcept {
    return memory_pool + (index * 4096);
  }

  inline uint16_t allocate_page() noexcept {
    uint16_t assigned_page = next_free_page++;
    PriceLevel *page_start = get_page(assigned_page);

    for (int i = 0; i < 4096; ++i) {
      page_start[i].head_index = NULL_INDEX;
      page_start[i].tail_index = NULL_INDEX;
    }

    return assigned_page;
  }
};

class PagedPriceArray {
private:
  uint16_t pages[512];

public:
  PagedPriceArray() {
    std::fill(std::begin(pages), std::end(pages), NULL_SLOT);
  }

  inline PriceLevel &get_level(uint32_t dense_price,
                               PageAllocator &global_alloc) noexcept {
    uint32_t page_id = dense_price >> 12;
    uint32_t offset = dense_price & 0x0FFF;

    if (__builtin_expect(pages[page_id] == NULL_SLOT, 0)) {
      pages[page_id] = global_alloc.allocate_page();
    }

    return global_alloc.get_page(pages[page_id])[offset];
  }
};
