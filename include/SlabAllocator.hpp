#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

template <typename T, uint32_t SLAB_BITS = 20> class SlabAllocator {
private:
  static constexpr uint32_t SLAB_SIZE = 1 << SLAB_BITS;
  static constexpr uint32_t SLAB_MASK = SLAB_SIZE - 1;

  std::vector<T *> slabs;
  std::vector<uint32_t> free_list;
  uint32_t total_capacity = 0;

  void allocate_slab() {
    T *newSlab = new T[SLAB_SIZE];
    slabs.push_back(newSlab);
    uint32_t base_index = total_capacity;
    uint32_t start_offset = (base_index == 0) ? 1 : 0;

    free_list.reserve(total_capacity + SLAB_SIZE);

    for (uint32_t i = SLAB_SIZE; i-- > start_offset;) {
      if (base_index == 0 && i == 0)
        break;

      // push the new added slab indexes into the free_list
      // but won't this push_back trigger copy when size increases?
      // no actually we have reserved memory upfront so no copy and resize
      // and shrinking doesn't happen so .... noe worries
      free_list.push_back(base_index + i);
    }

    total_capacity += SLAB_SIZE;
  }

public:
  SlabAllocator() { allocate_slab(); }

  ~SlabAllocator() {
    for (T *slab : slabs) {
      delete[] slab;
    }
  }

  uint32_t allocate() {
    if (free_list.empty()) {
      allocate_slab();
    }

    uint32_t index = free_list.back();
    free_list.pop_back();

    return index;
  }

  void free(uint32_t index) { free_list.push_back(index); }

  inline T &get(uint32_t index) {
    // first 12 bits of index to show the slab
    // next 20 bits to show the index within the slab
    uint32_t slab = index >> SLAB_BITS;
    uint32_t slot_index = index & SLAB_MASK;

    return slabs[slab][slot_index];
  }
};
