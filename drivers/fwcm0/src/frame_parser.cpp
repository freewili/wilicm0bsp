#include "fwcm0/frame_parser.h"
#include "fwcm0/crc8.h"
#include "fwcm0/cobs.h"
namespace fwcm0 {

void FrameParser::feed(const uint8_t* data, size_t n, const std::function<void(const Frame&)>& on_frame){
  for (size_t i = 0; i < n; ++i) {
    uint8_t b = data[i];
    if (b != 0x00) { buf_.push_back(b); continue; }
    // delimiter: decode the accumulated block
    if (!buf_.empty()) {
      std::vector<uint8_t> pf = cobs_decode(buf_);
      buf_.clear();
      if (pf.size() >= 5) {                       // type+seq+len(2)+crc minimum
        uint8_t crc = pf.back();
        if (crc8(pf.data(), pf.size()-1) == crc) {
          Frame f; f.type = pf[0]; f.seq = pf[1];
          f.len = static_cast<uint16_t>((uint16_t(pf[2])<<8) | pf[3]);
          size_t payload_n = pf.size() - 5;       // minus type,seq,len(2),crc
          if (payload_n == static_cast<size_t>(f.len)) {
            f.payload.assign(pf.begin()+4, pf.end()-1);
            on_frame(f);
          }
        }
      }
    } else { buf_.clear(); }                       // lone 0x00: skip
  }
}

}

