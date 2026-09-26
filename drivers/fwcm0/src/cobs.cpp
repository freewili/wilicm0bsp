#include "fwcm0/cobs.h"
namespace fwcm0 {
std::vector<uint8_t> cobs_encode(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out;
  size_t code_pos = out.size(); out.push_back(0x01); uint8_t code = 0x01;
  for (uint8_t b : in) {
    if (b != 0x00) { out.push_back(b); if (++code == 0xFF) { out[code_pos]=code; code_pos=out.size(); out.push_back(0x01); code=0x01; } }
    else { out[code_pos]=code; code_pos=out.size(); out.push_back(0x01); code=0x01; }
  }
  out[code_pos] = code;
  return out;
}
std::vector<uint8_t> cobs_decode(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out; size_t i = 0;
  while (i < in.size()) {
    uint8_t code = in[i++];
    if (code == 0) break;
    for (uint8_t j = 1; j < code && i < in.size(); ++j) out.push_back(in[i++]);
    if (code < 0xFF && i < in.size()) out.push_back(0x00);
  }
  return out;
}
}
