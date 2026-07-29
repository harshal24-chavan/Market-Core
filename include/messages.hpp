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
  uint8_t timeStamp[6];
  uint64_t orderRefNumber;
  char buySellIndicator;
  uint32_t shares;
  char stock[8];
  uint32_t price;
};

// 'E' - Order Executed (A resting order traded)
struct OrderExecuted {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timeStamp[6];
  uint64_t orderRefNumber;
  uint32_t executedShares;
  uint64_t matchNumber;
};

// 'X' - Order Cancel (Partial reduction of shares)
struct OrderCancel {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timeStamp[6];
  uint64_t orderRefNumber;
  uint32_t canceledShares;
};

// 'D' - Order Delete (Complete removal of the order)
struct OrderDelete {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timeStamp[6];
  uint64_t orderRefNumber;
};

// 'U' - Order Replace (Change in price or size)
struct OrderReplace {
  char messageType;
  uint16_t stockLocate;
  uint16_t trackingNumber;
  uint8_t timeStamp[6];
  uint64_t originalOrderRefNumber;
  uint64_t newOrderRefNumber;
  uint32_t shares;
  uint32_t price;
};

#pragma pack(pop)
