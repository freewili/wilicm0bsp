#include <gtest/gtest.h>
#include "fwcm0/sram_router.h"
#include "fwcm0/mock_transport.h"
#include <thread>
#include <chrono>
using namespace fwcm0;
TEST(SramRouter, ReadReturnsPayload){
  MockTransport t; SramRouter r(t);
  std::thread responder([&]{
    for(int i=0;i<200;++i){ { std::lock_guard<std::mutex> l(t.cmd_mutex());
      for(auto& c : t.commands) if(!c.empty() && c[0]==OP_READ_AT){ t.inject_frame(FT_READ_RESP, c[1], {0xAA,0xBB,0xCC,0xDD}); return; } }
      std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  });
  auto data = r.read(0x1000, 4, 1000);
  responder.join();
  EXPECT_EQ(data, (std::vector<uint8_t>{0xAA,0xBB,0xCC,0xDD}));
}
TEST(SramRouter, StatusParsesFlags){
  MockTransport t; SramRouter r(t);
  std::thread resp([&]{ for(int i=0;i<200;++i){ { std::lock_guard<std::mutex> l(t.cmd_mutex());
    for(auto& c: t.commands) if(!c.empty() && c[0]==OP_STATUS){ t.inject_frame(FT_STATUS, c[1], {static_cast<uint8_t>(FLAG_INIT_DONE|FLAG_BOOT_READY|FLAG_WR_ACTIVE), 0x37, 0,0,0,42}); return; } }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
  Status s = r.status(1000); resp.join();
  EXPECT_TRUE(s.init_done); EXPECT_TRUE(s.boot_ready); EXPECT_TRUE(s.wr_active);
  EXPECT_EQ(s.last_completed_seq, 0x37); EXPECT_EQ(s.bytes_written, 42u);
}
TEST(SramRouter, ReadTimesOutWhenNoResponse){
  MockTransport t; SramRouter r(t);
  EXPECT_THROW(r.read(0x10, 4, 50), TimeoutError);
}
TEST(SramRouter, NakThrows){
  MockTransport t; SramRouter r(t);
  std::thread resp([&]{ for(int i=0;i<200;++i){ { std::lock_guard<std::mutex> l(t.cmd_mutex());
    for(auto& c: t.commands) if(!c.empty() && c[0]==OP_READ_AT){ t.inject_frame(FT_NAK, c[1], {NAK_ABORTED_READ}); return; } }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
  EXPECT_THROW(r.read(0x10, 4, 1000), NakError); resp.join();
}
TEST(SramRouter, RequestSwapSendsOpcode){
  MockTransport t; SramRouter r(t);
  r.request_swap();
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  ASSERT_EQ(t.commands.size(), 1u);
  EXPECT_EQ(t.commands[0], (std::vector<uint8_t>{OP_SWAP_REQUEST}));
}
TEST(SramRouter, RequestSwapAndWaitReturnsOnNotify){
  MockTransport t; SramRouter r(t);
  std::thread resp([&]{ for(int i=0;i<200;++i){ { std::lock_guard<std::mutex> l(t.cmd_mutex());
    for(auto& c: t.commands) if(!c.empty() && c[0]==OP_SWAP_REQUEST){ t.inject_frame(FT_NOTIFY, 0, {NS_SWAP_READY}); return; } }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
  EXPECT_TRUE(r.request_swap_and_wait(1000)); resp.join();
}
TEST(SramRouter, RequestSwapAndWaitTimesOut){
  MockTransport t; SramRouter r(t);
  EXPECT_FALSE(r.request_swap_and_wait(50));
}
TEST(SramRouter, ReadStitchesAcrossFrames){
  MockTransport t; SramRouter r(t);
  std::thread responder([&]{
    bool sent_at=false, sent_cont=false;
    for(int i=0;i<400 && !(sent_at&&sent_cont); ++i){
      { std::lock_guard<std::mutex> l(t.cmd_mutex());
        for(auto& c : t.commands){
          if(!sent_at && !c.empty() && c[0]==OP_READ_AT){ t.inject_frame(FT_READ_RESP, c[1], std::vector<uint8_t>(4096, 0xA1)); sent_at=true; }
          else if(!sent_cont && !c.empty() && c[0]==OP_READ_CONT){ t.inject_frame(FT_READ_RESP, c[1], std::vector<uint8_t>(1904, 0xB2)); sent_cont=true; }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  auto data = r.read(0x2000, 6000, 2000);
  responder.join();
  ASSERT_EQ(data.size(), 6000u);
  EXPECT_EQ(data[0], 0xA1); EXPECT_EQ(data[4095], 0xA1);
  EXPECT_EQ(data[4096], 0xB2); EXPECT_EQ(data[5999], 0xB2);
}
