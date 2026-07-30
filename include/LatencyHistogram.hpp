#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <x86intrin.h>

enum MessageTypeIdx : size_t {
  MSG_ADD = 0,     // 'A', 'F'
  MSG_EXECUTE = 1, // 'E', 'C'
  MSG_CANCEL = 2,  // 'X'
  MSG_DELETE = 3,  // 'D'
  MSG_REPLACE = 4, // 'U'
  MSG_COUNT = 5
};

struct LatencyHistogram {
  static constexpr uint64_t MAX_DIRECT_CYCLES = 2048;

  // Direct buckets for 0-2047 cycles + 128 logarithmic overflow buckets
  std::array<uint64_t, MAX_DIRECT_CYCLES> direct_buckets{};
  std::array<uint64_t, 128> overflow_buckets{};

  uint64_t total_count = 0;
  uint64_t total_cycles = 0;
  uint64_t min_cycles = ~0ULL;
  uint64_t max_cycles = 0;

  [[gnu::always_inline]] inline void record(uint64_t cycles) noexcept {
    total_count++;
    total_cycles += cycles;
    if (cycles < min_cycles)
      min_cycles = cycles;
    if (cycles > max_cycles)
      max_cycles = cycles;

    if (cycles < MAX_DIRECT_CYCLES) {
      direct_buckets[cycles]++;
    } else {
      // Logarithmic binning for tail latencies (>2048 cycles)
      uint32_t log_bin = 64 - __builtin_clzll(cycles);
      if (log_bin < 128) {
        overflow_buckets[log_bin]++;
      } else {
        overflow_buckets[127]++;
      }
    }
  }

  // Helper to calculate percentiles (p50, p90, p99, p99.9)
  uint64_t get_percentile(double p) const {
    if (total_count == 0)
      return 0;
    uint64_t target_rank = static_cast<uint64_t>(std::ceil(p * total_count));
    uint64_t current_rank = 0;

    for (size_t i = 0; i < MAX_DIRECT_CYCLES; ++i) {
      current_rank += direct_buckets[i];
      if (current_rank >= target_rank)
        return i;
    }

    for (size_t i = 0; i < 128; ++i) {
      current_rank += overflow_buckets[i];
      if (current_rank >= target_rank) {
        return 1ULL << i; // Approximate boundary for log bin
      }
    }
    return max_cycles;
  }
};

[[gnu::always_inline]] inline uint64_t rdtsc_start() noexcept {
  _mm_lfence();
  return __rdtsc();
}

[[gnu::always_inline]] inline uint64_t rdtsc_end() noexcept {
  uint32_t aux;
  uint64_t t = __rdtscp(&aux);
  _mm_lfence();
  return t;
}
