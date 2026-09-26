#include <gtest/gtest.h>
#include "fwcm0/bridge_proto.h"
#include "fwcm0/transport.h"   // Status
#include <vector>
using namespace fwcm0;
using B = std::vector<uint8_t>;

TEST(BridgeProto, ReadReqRoundTrip) {
  B req = bridge::encode_read_req(0x00123456, 300);
  uint32_t addr = 0; uint16_t len = 0;
  ASSERT_TRUE(bridge::decode_read_req(req.data() + 1, req.size() - 1, addr, len)); // skip op byte
  EXPECT_EQ(addr, 0x00123456u);
  EXPECT_EQ(len, 300);
  EXPECT_EQ(req[0], bridge::OP_READ);
}

TEST(BridgeProto, WriteReqRoundTrip) {
  B data{0xDE, 0xAD, 0xBE, 0xEF};
  B req = bridge::encode_write_req(0x40, data);
  uint32_t addr = 0; B out;
  ASSERT_TRUE(bridge::decode_write_req(req.data() + 1, req.size() - 1, addr, out));
  EXPECT_EQ(addr, 0x40u);
  EXPECT_EQ(out, data);
}

TEST(BridgeProto, StatusRespRoundTrip) {
  Status s{}; s.init_done = 1; s.ready = 1; s.last_completed_seq = 0x2A; s.bytes_written = 4096;
  B resp = bridge::encode_status_resp(s);
  EXPECT_EQ(resp[0], bridge::RC_OK);
  Status d{};
  ASSERT_TRUE(bridge::decode_status_resp(resp, d));
  EXPECT_EQ(d.init_done, 1);
  EXPECT_EQ(d.ready, 1);
  EXPECT_EQ(d.last_completed_seq, 0x2A);
  EXPECT_EQ(d.bytes_written, 4096u);
}

TEST(BridgeProto, DecodeRejectsTruncated) {
  uint32_t addr; uint16_t len;
  EXPECT_FALSE(bridge::decode_read_req(nullptr, 0, addr, len));
  B tooShort{0x00, 0x01}; // needs 6 bytes (u32 + u16)
  EXPECT_FALSE(bridge::decode_read_req(tooShort.data(), tooShort.size(), addr, len));
}
