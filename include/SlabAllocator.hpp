#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>

template <typename T, uint32_t SLAB_BITS = 22> class SlabAllocator {
private:
  static constexpr uint32_t SLAB_SIZE = 1U << SLAB_BITS;
  static constexpr uint32_t SLAB_MASK = SLAB_SIZE - 1;
  static constexpr uint32_t MAX_SLABS = 64; // Supports up to ~67 Million Orders
  // according to calculation it actually only uses 1 slab for the entire run

  T *slabs[MAX_SLABS]{nullptr};
  uint32_t num_slabs = 0;
  uint32_t head_free =
      std::numeric_limits<uint32_t>::max(); // Intrusive Free-List Head

  void allocate_slab() {
    std::cout << "slab allocation: " << num_slabs << "\n";

    assert(num_slabs < MAX_SLABS && "Exceeded maximum slab capacity!");

    size_t raw_slab_bytes = sizeof(T) * SLAB_SIZE;
    constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;
    size_t slab_bytes =
        (raw_slab_bytes + (HUGE_PAGE_SIZE - 1)) & ~(HUGE_PAGE_SIZE - 1);

    void *raw_ptr =
        ::mmap(nullptr, slab_bytes, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);

    if (raw_ptr == MAP_FAILED) {
      throw std::runtime_error("No Huge Page SlabAllocator...");
    }

    T *newSlab = static_cast<T *>(raw_ptr);
    slabs[num_slabs] = newSlab;

    uint32_t base_index = num_slabs * SLAB_SIZE;
    uint32_t start_offset = (base_index == 0) ? 1 : 0;

    // Build the intrusive list from HIGHEST index down to LOWEST index.
    // This guarantees head_free ends up at 'base_index + start_offset',
    // forcing allocate() to hand out memory in ASCENDING ORDER (1, 2, 3, 4...).
    for (uint32_t i = SLAB_SIZE; i-- > start_offset;) {
      uint32_t global_index = base_index + i;
      *reinterpret_cast<uint32_t *>(&newSlab[i]) = head_free;
      head_free = global_index;
    }

    num_slabs++;
  }

public:
  SlabAllocator() { allocate_slab(); }

  ~SlabAllocator() {
    size_t raw_slab_bytes = sizeof(T) * SLAB_SIZE;
    constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;
    size_t slab_bytes =
        (raw_slab_bytes + (HUGE_PAGE_SIZE - 1)) & ~(HUGE_PAGE_SIZE - 1);

    for (uint32_t i = 0; i < num_slabs; i++) {
      if (slabs[i]) {
        ::munmap(slabs[i], slab_bytes);
      }
    }
  }

  uint32_t allocate() noexcept {
    if (head_free == std::numeric_limits<uint32_t>::max()) {
      allocate_slab();
    }

    uint32_t allocated_index = head_free;

    // Read the next free index stored inside the slot being allocated
    T &slot = get(allocated_index);
    head_free = *reinterpret_cast<uint32_t *>(&slot);

    return allocated_index;
  }

  void free(uint32_t index) noexcept {
    // Intrusively link freed node back into the head of the free list
    T &slot = get(index);
    *reinterpret_cast<uint32_t *>(&slot) = head_free;
    head_free = index;
  }

  inline T &get(uint32_t index) noexcept {
    uint32_t slab = index >> SLAB_BITS;
    uint32_t slot_index = index & SLAB_MASK;

    return slabs[slab][slot_index];
  }

  inline const T &get(uint32_t index) const noexcept {
    uint32_t slab = index >> SLAB_BITS;
    uint32_t slot_index = index & SLAB_MASK;

    return slabs[slab][slot_index];
  }
};
