#include <cassert>
#include <cstdint>
#include <emmintrin.h>

constexpr uint8_t CTRL_EMPTY = -128;
constexpr uint8_t CTRL_DELETED = -2;

struct alignas(16) HashEntry {
  uint64_t key;
  uint32_t value;
  uint32_t locate_code;
};

class SIMDOrderMap {
private:
  uint32_t capacity{0};
  uint32_t capacity_mask{0};

  // 1-byte metadata per slot. Aligned for fast SIMD loads.
  alignas(16) int8_t *ctrl;
  alignas(16) HashEntry *slots;

public:
  SIMDOrderMap(uint32_t capacity_bits = 27) {
    capacity = 1ULL << capacity_bits;
    capacity_mask = capacity - 1;

    ctrl = new int8_t[capacity];
    for (uint32_t i = 0; i < capacity; i++)
      ctrl[i] = CTRL_EMPTY;

    slots = new HashEntry[capacity]();
  }

  ~SIMDOrderMap() {
    delete[] ctrl;
    delete[] slots;
  }

  // Split the 64-bit hash into H1 (for indexing) and H2 (for the control byte)
  inline uint32_t get_index(uint64_t hash) const {
    return hash & capacity_mask;
  }
  inline int8_t get_ctrl_byte(uint64_t hash) const {
    return hash >> 57;
  } // Top 7 bits

  inline HashEntry *get(uint64_t key) const noexcept {
    uint64_t hash = raw_hash(key);

    uint32_t index = get_index(hash);
    int8_t ctrl_byte = get_ctrl_byte(hash);

    __m128i match_vec = _mm_set1_epi8(ctrl_byte);

    while (true) {
      __m128i ctrl_vec =
          _mm_loadu_si128(reinterpret_cast<const __m128i *>(ctrl_byte));

      // SIMD Compare: Sets byte to 0xFF if equal, 0x00 if not
      __m128i cmp = _mm_cmpeq_epi8(ctrl_vec, match_vec);

      // Extract the highest bit of each byte into a 16-bit integer mask
      uint32_t match_mask = _mm_movemask_epi8(cmp);
      while (match_mask != 0) {
        uint32_t bit_pos = __builtin_ctz(match_mask);
        uint32_t match_index = (index + bit_pos) & capacity_mask;
        if (slots[match_index].key == key) {
          return &slots[match_index];
        }

        match_mask = match_mask & (match_mask - 1);
      }

      // if there are empty slots then the search is over key doesn't exist
      __m128i empty_vec = _mm_set1_epi8(CTRL_EMPTY);
      __m128i empty_cmp = _mm_cmpeq_epi8(ctrl_vec, empty_vec);
      if (_mm_movemask_epi8(empty_cmp) != 0) {
        return nullptr;
      }

      index = (index + 16) & capacity_mask;
    }
  }

  inline uint64_t raw_hash(uint64_t key) const noexcept {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    return key;
  }
};
