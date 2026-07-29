#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>

template <typename T, uint32_t SLAB_BITS = 20> class SlabAllocator {
private:
  static constexpr uint32_t SLAB_SIZE = 1U << SLAB_BITS;
  static constexpr uint32_t SLAB_MASK = SLAB_SIZE - 1;
  static constexpr uint32_t MAX_SLABS = 64; // Supports up to ~67 Million Orders

  T *slabs[MAX_SLABS]{nullptr};
  uint32_t num_slabs = 0;
  uint32_t head_free =
      std::numeric_limits<uint32_t>::max(); // Intrusive Free-List Head

  void allocate_slab() {
    assert(num_slabs < MAX_SLABS && "Exceeded maximum slab capacity!");

    size_t slab_bytes = sizeof(T) * SLAB_SIZE;
    void *raw_ptr = nullptr;

    int res = posix_memalign(&raw_ptr, 2 * 1024 * 1024, slab_bytes);
    assert(res == 0 && "Failed to allocate 2MB aligned memory slab!");

    T *newSlab = static_cast<T *>(raw_ptr);
    madvise(newSlab, slab_bytes, MADV_HUGEPAGE);

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
    for (uint32_t i = 0; i < num_slabs; ++i) {
      if (slabs[i]) {
        std::free(slabs[i]);
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
