#include "fwcm0/crc8.h"
#include "fwcm0/protocol.h"
namespace fwcm0 {
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b)
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ CRC8_POLY)
                         : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}
}
