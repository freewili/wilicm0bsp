#include <gtest/gtest.h>
#include "uart_receiver.h"
#include "fwcm0/mock_transport.h"
#include <atomic>
using namespace fwcm0;
TEST(UartReceiver, DeliversInjectedFrames){
  MockTransport t; std::vector<Frame> got; std::mutex gm;
  UartReceiver rx(t, [&](const Frame& f){ std::lock_guard<std::mutex> l(gm); got.push_back(f); });
  rx.start();
  t.inject_frame(FT_READ_RESP, 0x10, {1,2,3});
  t.inject_frame(FT_NOTIFY, 0x00, {NS_SWAP_READY});
  // poll up to ~1s for two frames
  for(int i=0;i<100 && [&]{std::lock_guard<std::mutex> l(gm); return got.size()<2;}();++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  rx.stop();
  std::lock_guard<std::mutex> l(gm);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0].type, FT_READ_RESP); EXPECT_EQ(got[0].payload, (std::vector<uint8_t>{1,2,3}));
  EXPECT_EQ(got[1].type, FT_NOTIFY);    EXPECT_EQ(got[1].payload, (std::vector<uint8_t>{NS_SWAP_READY}));
}
