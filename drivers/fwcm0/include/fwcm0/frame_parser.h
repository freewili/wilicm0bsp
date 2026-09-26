#pragma once
#include <cstdint>
#include <vector>
#include <functional>
namespace fwcm0 {

struct Frame {
  uint8_t type;
  uint8_t seq;
  uint16_t len;
  std::vector<uint8_t> payload;
};

class FrameParser {
public:
  // Accumulates bytes; on each 0x00 delimiter, COBS-decodes + CRC-checks the
  // block and, if valid and self-consistent (len matches), calls on_frame.
  void feed(const uint8_t* data, size_t n, const std::function<void(const Frame&)>& on_frame);
private:
  std::vector<uint8_t> buf_;   // raw COBS bytes since the last delimiter
};

}
