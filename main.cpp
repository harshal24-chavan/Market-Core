#include "include/MmappedFile.hpp"
#include "include/messages.hpp"
#include <chrono>
#include <ctime>
#include <iostream>

int main() {
  MmappedFile itchFile("./01302020.NASDAQ_ITCH50.1");

  const char *current = itchFile.data();
  const char *end = current + itchFile.size();

  std::cout << "successfully mapped files of size: " << itchFile.size()
            << " bytes." << std::endl;

  size_t addOrderCount{0};

  auto start = std::chrono::high_resolution_clock::now();

  while (current < end) {
    // const uint16_t *len_ptr = reinterpret_cast<const uint16_t *>(current);
    // uint16_t raw_len = *len_ptr;
    // uint16_t length = bswap16(raw_len);
    // same as below;
    uint16_t length = bswap16(*reinterpret_cast<const uint16_t *>(current));

    current += 2;

    char messageType = *current;

    if (messageType == 'A') {
      addOrderCount++;
      const AddOrder *order = reinterpret_cast<const AddOrder *>(current);
    }

    current += length;
  }

  auto end = std::chrono::high_resolution_clock::now();

  std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "\n";

  std::cout << "add order count: " << addOrderCount << std::endl;

  return 0;
}
