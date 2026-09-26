#include <gtest/gtest.h>
#include "fwcm0/frame_parser.h"
#include "fwcm0/protocol.h"
#include "fwcm0/crc8.h"
#include "fwcm0/cobs.h"
using namespace fwcm0; using B = std::vector<uint8_t>;

// helper: build a wire frame = COBS([type][seq][len][payload][crc]) + 0x00
static B wire(uint8_t type, uint8_t seq, const B& payload){
  B pf{type, seq, (uint8_t)(payload.size()>>8), (uint8_t)(payload.size()&0xFF)};
  pf.insert(pf.end(), payload.begin(), payload.end());
  pf.push_back(crc8(pf.data(), pf.size()));
  B w = cobs_encode(pf); w.push_back(0x00); return w;
}

TEST(FrameParser, ParsesGoodFrameAndDispatchesFields){
  FrameParser p; std::vector<Frame> got;
  B w = wire(FT_READ_RESP, 0x21, {0xDE,0xAD,0xBE,0xEF});
  p.feed(w.data(), w.size(), [&](const Frame& f){ got.push_back(f); });
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].type, FT_READ_RESP); EXPECT_EQ(got[0].seq, 0x21);
  EXPECT_EQ(got[0].payload, (B{0xDE,0xAD,0xBE,0xEF}));
}

TEST(FrameParser, DropsBadCrcThenRecovers){
  FrameParser p; std::vector<Frame> got;
  B bad = wire(FT_STATUS, 0x01, {0,0,0,0,0,0}); bad[2] ^= 0xFF;  // corrupt a byte
  B good = wire(FT_NAK, 0x02, {0xFF});
  B s = bad; s.insert(s.end(), good.begin(), good.end());
  p.feed(s.data(), s.size(), [&](const Frame& f){ got.push_back(f); });
  ASSERT_EQ(got.size(), 1u); EXPECT_EQ(got[0].type, FT_NAK); EXPECT_EQ(got[0].seq, 0x02);
}

TEST(FrameParser, SplitAcrossFeeds){
  FrameParser p; std::vector<Frame> got;
  B w = wire(FT_STATUS, 0x44, {0xC0,0x37,0,0,0,5});
  p.feed(w.data(), 3, [&](const Frame& f){ got.push_back(f); });
  p.feed(w.data()+3, w.size()-3, [&](const Frame& f){ got.push_back(f); });
  ASSERT_EQ(got.size(), 1u); EXPECT_EQ(got[0].seq, 0x44);
}
