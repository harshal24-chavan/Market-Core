#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>

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
    size_t bytes = max_pages * 4096 * sizeof(PriceLevel);

    // Allocate anonymously. Use huge pages if available.
    memory_pool =
        static_cast<PriceLevel *>(::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    if (memory_pool == MAP_FAILED)
      throw std::runtime_error("Page Allocator OOM");

    ::madvise(memory_pool, bytes, MADV_HUGEPAGE);
    std::memset(memory_pool, 0, bytes); // Pre-fault physical memory
  }

  inline PriceLevel *allocate_page() noexcept {
    return &memory_pool[(next_free_page++) * 4096];
  }
};

class PagedPriceArray {
private:
  PriceLevel *pages[2048] = {nullptr};

public:
  inline PriceLevel &get_level(uint32_t dense_price,
                               PageAllocator &global_alloc) noexcept {
    uint32_t page_id = dense_price >> 12;
    uint32_t offset = dense_price & 0x0FFF;

    if (__builtin_expect(pages[page_id] == nullptr, 0)) {
      pages[page_id] = global_alloc.allocate_page();
    }
    return pages[page_id][offset];
  }
};
