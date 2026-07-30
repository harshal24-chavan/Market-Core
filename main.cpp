#include "include/FlatHashTable.hpp"
#include "include/LatencyHistogram.hpp"
#include "include/LimitOrderBook.hpp"
#include "include/MmappedFile.hpp"
#include "include/messages.hpp"
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>

static LatencyHistogram g_histograms[MSG_COUNT];

inline uint32_t to_dense_index(uint32_t itch_price) noexcept {
  // Sub-dollar prices ($0.0000 to $0.9999) keep 0.0001 granularity
  if (itch_price < 10000) {
    return itch_price;
  }
  // Prices >= $1.00 are scaled to 0.01 granularity (cents)
  // Offset by 10000 so they seamlessly continue after the sub-dollar range
  return 10000 + ((itch_price - 10000) / 100);
}

void pin_thread_to_core(int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  pthread_t current_thread = pthread_self();

  int result =
      pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
  if (result != 0) {
    std::cerr << "Warning: Failed to pin thread to core " << core_id
              << ". Error code: " << result << "\n";
  } else {
    std::cout << "Successfully pinned main thread to CPU Core " << core_id
              << "\n";
  }
}

void print_latency_report(double cpu_ghz = 3.07) {
  const char *names[MSG_COUNT] = {"Add Order ('A'/'F')", "Execute ('E'/'C')",
                                  "Cancel ('X')", "Delete ('D')",
                                  "Replace ('U')"};
  double cycles_to_ns = 1.0 / cpu_ghz;

  std::cout << "\n============================================================="
               "===========================\n";
  std::cout << "                          NASDAQ ITCH 5.0 LATENCY HISTOGRAM    "
               "                         \n";
  std::cout << "==============================================================="
               "=========================\n";
  std::cout << std::left << std::setw(20) << "Message Type" << std::setw(12)
            << "Count" << std::setw(12) << "Avg (ns)" << std::setw(10)
            << "Min (ns)" << std::setw(10) << "p50 (ns)" << std::setw(10)
            << "p90 (ns)" << std::setw(10) << "p99 (ns)" << std::setw(12)
            << "p99.9 (ns)" << std::setw(10) << "Max (ns)" << "\n";
  std::cout << "---------------------------------------------------------------"
               "-------------------------\n";

  for (size_t i = 0; i < MSG_COUNT; ++i) {
    const auto &h = g_histograms[i];
    if (h.total_count == 0)
      continue;

    double avg_cycles = static_cast<double>(h.total_cycles) / h.total_count;

    std::cout << std::left << std::setw(20) << names[i] << std::setw(12)
              << h.total_count << std::setw(12) << std::fixed
              << std::setprecision(1) << (avg_cycles * cycles_to_ns)
              << std::setw(10)
              << static_cast<uint64_t>(h.min_cycles * cycles_to_ns)
              << std::setw(10)
              << static_cast<uint64_t>(h.get_percentile(0.50) * cycles_to_ns)
              << std::setw(10)
              << static_cast<uint64_t>(h.get_percentile(0.90) * cycles_to_ns)
              << std::setw(10)
              << static_cast<uint64_t>(h.get_percentile(0.99) * cycles_to_ns)
              << std::setw(12)
              << static_cast<uint64_t>(h.get_percentile(0.999) * cycles_to_ns)
              << std::setw(10)
              << static_cast<uint64_t>(h.max_cycles * cycles_to_ns) << "\n";
  }
  std::cout << "==============================================================="
               "=========================\n";
}

int main() {
  pin_thread_to_core(2);

  // 1. Static Allocation
  static LimitOrderbook lob;

  try {
    // 2. Map the file directly into virtual memory
    MmappedFile file("01302020.NASDAQ_ITCH50.1");

    const char *ptr = file.data();
    const char *end = ptr + file.size();
    uint64_t message_count = 0;

    std::cout << "Starting Deterministic Replay Engine...\n";

    // 3. The mmap traversal loop
    while (ptr < end) {
      // ITCH historical files prefix each message with a 2-byte length
      // We use reinterpret_cast and immediately byte-swap it.
      uint16_t msg_length = bswap16(*reinterpret_cast<const uint16_t *>(ptr));
      ptr += 2; // Move past the length prefix

      if (ptr + msg_length > end)
        break; // Safety check

      char msg_type = *ptr;

      // 4. Parse, Byte-Swap, and Route
      switch (msg_type) {
      case 'A':
      case 'F': {
        const auto *raw_msg = reinterpret_cast<const AddOrder *>(ptr);
        AddOrder msg = *raw_msg; // Copy so we can safely mutate the bytes

        msg.orderRefNumber = bswap64(msg.orderRefNumber);
        msg.shares = bswap32(msg.shares);
        msg.price = to_dense_index(bswap32(msg.price));

        uint64_t start = rdtsc_start();
        lob.on_add_message(&msg);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_ADD].record(elapsed);
        break;
      }
      case 'D': {
        const auto *raw_msg = reinterpret_cast<const OrderDelete *>(ptr);
        OrderDelete msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);

        uint64_t start = rdtsc_start();
        lob.on_delete_message(&msg);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_DELETE].record(elapsed);
        break;
      }
      case 'E': {
        const auto *raw_msg = reinterpret_cast<const OrderExecuted *>(ptr);
        OrderExecuted msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);
        msg.executedShares = bswap32(msg.executedShares);

        uint64_t start = rdtsc_start();
        lob.on_execute_message(&msg);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_EXECUTE].record(elapsed);
        break;
      }
      case 'X': {
        const auto *raw_msg = reinterpret_cast<const OrderCancel *>(ptr);
        OrderCancel msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);
        msg.canceledShares = bswap32(msg.canceledShares);

        uint64_t start = rdtsc_start();
        lob.on_cancel_message(&msg);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_CANCEL].record(elapsed);
        break;
      }
      case 'U': {
        const auto *raw_msg = reinterpret_cast<const OrderReplace *>(ptr);
        OrderReplace msg = *raw_msg;

        msg.originalOrderRefNumber = bswap64(msg.originalOrderRefNumber);
        msg.newOrderRefNumber = bswap64(msg.newOrderRefNumber);
        msg.shares = bswap32(msg.shares);
        msg.price = to_dense_index(bswap32(msg.price));

        uint64_t start = rdtsc_start();
        lob.on_replace_message(&msg);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_REPLACE].record(elapsed);
        break;
      }
      }

      // 5. The Crucible - Assert mathematical perfection
      // lob.validate_invariants();

      // Advance the pointer to the next message
      ptr += msg_length;
      message_count++;
    }

    std::cout << "Replay completed successfully. " << message_count << "\n";

    print_latency_report();

  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
