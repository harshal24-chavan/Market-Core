#pragma once

#include <cstdint>

inline uint16_t bswap16(uint16_t val) { return __builtin_bswap16(val); }
inline uint32_t bswap32(uint32_t val) { return __builtin_bswap32(val); }
inline uint64_t bswap64(uint64_t val) { return __builtin_bswap64(val); }

inline uint64_t parse_timestamp(const uint8_t ts[6]) {
  uint64_t res{0};
  for (int i = 0; i < 6; i++) {
    res = (res << 8) | ts[i];
  }
  return res;
}

#pragma pack(push, 1)
struct AddOrder {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timeStamp;
  uint64_t orderRefNumber;
  char buySellIndicator;
  uint32_t shares;
  char stock[8];
  uint32_t price;
};
#pragma pack(pop)
