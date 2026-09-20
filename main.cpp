#include "include/FlatHashTable.hpp"
#include "include/LatencyHistogram.hpp"
#include "include/MarketManager.hpp"
#include "include/MmappedFile.hpp"
#include "include/messages.hpp"
#include <byteswap.h> // Ensure this is included for bswap_16, bswap_32, bswap_64
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>

static LatencyHistogram g_histograms[MSG_COUNT];

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
               "                     \n";
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

  // 1. Initialize the new multi-stock Market Manager
  MarketManager market;

  try {
    // 2. Stream the file using MAP_PRIVATE + MADV_SEQUENTIAL (Safe for 12GB RAM
    // limit)
    MmappedFile file("01302020.NASDAQ_ITCH50.1");

    const char *ptr = file.data();
    const char *end = ptr + file.size();
    uint64_t message_count = 0;
    uint64_t next_report = 100'000;

    std::cout << "Starting Deterministic Replay Engine...\n";

    // 3. The highly optimized branch-predicted loop
    while (__builtin_expect(ptr < end, 1)) {
      // Read the 2-byte message length
      uint16_t msg_length = bswap16(*reinterpret_cast<const uint16_t *>(ptr));

      // Move pointer to the start of the actual message payload
      const char *msg_ptr = ptr + 2;
      char msg_type = msg_ptr[0];

      switch (msg_type) {
      case 'A':
      case 'F': {
        const auto *msg = reinterpret_cast<const AddOrder *>(msg_ptr);

        // We must bswap the locate code here to route it to the correct stock
        // array index
        uint16_t locate = bswap16(msg->stockLocate);

        uint64_t start = rdtsc_start();
        market.process_add(
            msg, locate); // Manager handles remaining bswaps internally
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_ADD].record(elapsed);
        break;
      }
      case 'D': {
        const auto *msg = reinterpret_cast<const OrderDelete *>(msg_ptr);
        uint64_t order_id = bswap64(msg->orderRefNumber);

        uint64_t start = rdtsc_start();
        market.process_delete(order_id);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_DELETE].record(elapsed);
        break;
      }
      case 'E':
      case 'C': { // Treat 'C' (Execute with Price) exactly like 'E' for LOB
                  // state
        // Both structs have orderRefNumber and executedShares at the exact same
        // offsets
        const auto *msg = reinterpret_cast<const OrderExecuted *>(msg_ptr);
        uint64_t order_id = bswap64(msg->orderRefNumber);
        uint32_t shares = bswap32(msg->executedShares);

        uint64_t start = rdtsc_start();
        market.process_execute(order_id, shares);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_EXECUTE].record(elapsed);
        break;
      }
      case 'X': {
        const auto *msg = reinterpret_cast<const OrderCancel *>(msg_ptr);
        uint64_t order_id = bswap64(msg->orderRefNumber);
        uint32_t canceled_shares = bswap32(msg->canceledShares);

        uint64_t start = rdtsc_start();
        market.process_cancel(order_id, canceled_shares);
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_CANCEL].record(elapsed);
        break;
      }
      case 'U': {
        const auto *msg = reinterpret_cast<const OrderReplace *>(msg_ptr);

        uint64_t start = rdtsc_start();
        market.process_replace(msg); // Manager handles bswaps internally
        uint64_t elapsed = rdtsc_end() - start;
        g_histograms[MSG_REPLACE].record(elapsed);
        break;
      }
      }

      // 4. Advance pointer exactly to the next message chunk
      ptr += 2 + msg_length;
      message_count++;

      if (message_count == next_report) {
        std::cout << "Completed " << message_count << " messages\n";
        next_report += 100'000;
        std::cout << "drop count: " << drop_count << "\n";
      }
    }

    std::cout << "Replay completed successfully. Total Messages: "
              << message_count << "\n";
    print_latency_report();

  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
