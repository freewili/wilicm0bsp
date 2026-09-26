#pragma once
#include <cstdint>
#include <vector>
namespace fwcm0 {
std::vector<uint8_t> cobs_encode(const std::vector<uint8_t>& in);
// Decodes one COBS block (without the trailing 0x00 delimiter). Returns the
// decoded bytes; on a malformed block returns what decoded so far (callers
// CRC-check, so a corrupt frame is rejected downstream).
std::vector<uint8_t> cobs_decode(const std::vector<uint8_t>& in);
}
