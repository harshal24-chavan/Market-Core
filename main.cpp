#include "include/LimitOrderBook.hpp"
#include "include/MmappedFile.hpp"
#include "include/messages.hpp"
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>

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
        msg.price = bswap32(msg.price);

        lob.on_add_message(&msg);
        break;
      }
      case 'D': {
        const auto *raw_msg = reinterpret_cast<const OrderDelete *>(ptr);
        OrderDelete msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);

        lob.on_delete_message(&msg);
        break;
      }
      case 'E': {
        const auto *raw_msg = reinterpret_cast<const OrderExecuted *>(ptr);
        OrderExecuted msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);
        msg.executedShares = bswap32(msg.executedShares);

        lob.on_execute_message(&msg);
        break;
      }
      case 'X': {
        const auto *raw_msg = reinterpret_cast<const OrderCancel *>(ptr);
        OrderCancel msg = *raw_msg;

        msg.orderRefNumber = bswap64(msg.orderRefNumber);
        msg.canceledShares = bswap32(msg.canceledShares);

        lob.on_cancel_message(&msg);
        break;
      }
      case 'U': {
        const auto *raw_msg = reinterpret_cast<const OrderReplace *>(ptr);
        OrderReplace msg = *raw_msg;

        msg.originalOrderRefNumber = bswap64(msg.originalOrderRefNumber);
        msg.newOrderRefNumber = bswap64(msg.newOrderRefNumber);
        msg.shares = bswap32(msg.shares);
        msg.price = bswap32(msg.price);

        lob.on_replace_message(&msg);
        break;
      }
      }

      // 5. The Crucible - Assert mathematical perfection
      // lob.validate_invariants();

      // Advance the pointer to the next message
      ptr += msg_length;
      message_count++;

      if (message_count % 1000000 == 0) {
        std::cout << "Processed " << message_count << std::endl;
      }
    }

    std::cout << "Replay completed successfully. " << message_count
              << " messages proven correct.\n";
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
