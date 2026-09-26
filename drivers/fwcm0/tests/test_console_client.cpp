#include <gtest/gtest.h>
#include "fwcm0/commands.h"
#include "fwcm0/console_client.h"
#include "fwcm0/mbox_mux.h"
#include "fwcm0/mock_transport.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
using namespace fwcm0;
using B = std::vector<uint8_t>;

// (a) cmd_mbox byte layout
TEST(CmdMbox, Layout){
  EXPECT_EQ(cmd_mbox({0x00, 0x41, 0x42}), (B{OP_MBOX, 0x03, 0x00, 0x41, 0x42}));
  EXPECT_THROW(cmd_mbox({}), std::invalid_argument);
  EXPECT_EQ(cmd_mbox({0xFF}),              (B{OP_MBOX, 0x01, 0xFF}));
}

TEST(CmdMbox, RejectsOversizedBody){
  EXPECT_THROW(cmd_mbox(B(33, 0xAA)), std::invalid_argument);
}

TEST(CmdMbox, Accepts32ByteBody){
  EXPECT_NO_THROW(cmd_mbox(B(32, 0xBB)));
}

// helper: find the Nth SPI command with a given leading opcode
static const B* find_cmd(const MockTransport& t, uint8_t op, size_t n=0){
  size_t found=0;
  for(auto& c : t.commands) if(!c.empty() && c[0]==op){ if(found==n) return &c; ++found; }
  return nullptr;
}

// count OP_MBOX commands whose body is CONTROL + the given sub-op
static size_t count_ctrl(const MockTransport& t, uint8_t sub){
  size_t n=0;
  for(auto& c : t.commands)
    if(c.size()>=4 && c[0]==OP_MBOX && c[2]==MBOX_TYPE_CONTROL && c[3]==sub) ++n;
  return n;
}

// (b) set_stream/hello/reset/send_console produce correct SPI bytes
TEST(ConsoleClient, SetStreamOn){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  cc.set_stream(true);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX);
  ASSERT_NE(c, nullptr);
  // [OP_MBOX, len=3, CONTROL, SET_STREAM, 1]
  EXPECT_EQ(*c, (B{OP_MBOX, 0x03, MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, 0x01}));
}

TEST(ConsoleClient, SetStreamOff){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  cc.set_stream(false);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(*c, (B{OP_MBOX, 0x03, MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, 0x00}));
}

TEST(ConsoleClient, HelloCommand){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  cc.hello();
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(*c, (B{OP_MBOX, 0x02, MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO}));
}

TEST(ConsoleClient, ResetCommand){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  cc.reset();
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(*c, (B{OP_MBOX, 0x02, MBOX_TYPE_CONTROL, MBOX_CTRL_RESET}));
}

TEST(ConsoleClient, SendConsoleShort){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  const uint8_t msg[] = {'H','e','l','l','o'};
  cc.send_console(msg, sizeof(msg));
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX);
  ASSERT_NE(c, nullptr);
  // [OP_MBOX, 6, CONSOLE, 'H','e','l','l','o']
  B expected{OP_MBOX, 6, MBOX_TYPE_CONSOLE, 'H','e','l','l','o'};
  EXPECT_EQ(*c, expected);
}

// (c) chunking: >31 bytes yields multiple OP_MBOX commands each <=31 payload bytes
TEST(ConsoleClient, SendConsoleChunks){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  // 65 bytes -> ceil(65/31) = 3 chunks (31, 31, 3)
  B data(65, 0xAB);
  cc.send_console(data.data(), data.size());
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  size_t mbox_count = 0;
  for(auto& c : t.commands){
    if(c.empty() || c[0] != OP_MBOX) continue;
    ++mbox_count;
    uint8_t body_len = c[1];
    // body includes the CONSOLE type byte + data, so data portion <= 31
    EXPECT_LE(body_len, 32u); // total body <= 32
    ASSERT_GE(body_len, 1u);
    // payload after type byte
    EXPECT_LE(static_cast<size_t>(body_len - 1), 31u);
    // type byte must be CONSOLE
    ASSERT_GE(c.size(), 3u);
    EXPECT_EQ(c[2], MBOX_TYPE_CONSOLE);
  }
  EXPECT_EQ(mbox_count, 3u);
  // verify first chunk length: 1+31 = 32 body bytes
  auto* c0 = find_cmd(t, OP_MBOX, 0);
  ASSERT_NE(c0, nullptr);
  EXPECT_EQ((*c0)[1], 32u); // len byte = 32 (1 type + 31 data)
  // last chunk: 65 - 62 = 3 data bytes -> body len = 4
  auto* c2 = find_cmd(t, OP_MBOX, 2);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ((*c2)[1], 4u);
}

// (d) received FT_MBOX frame [CONSOLE, 'H','i'] delivers "Hi" to on_console
TEST(ConsoleClient, ReceiveConsoleFrame){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);

  B received;
  std::mutex m; std::condition_variable cv; bool got = false;
  cc.on_console([&](const B& data){
    std::lock_guard<std::mutex> l(m); received=data; got=true; cv.notify_one();
  });

  // inject FT_MBOX frame: payload = body = [CONSOLE, 'H', 'i']
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONSOLE, 'H', 'i'});

  std::unique_lock<std::mutex> ul(m);
  bool ok = cv.wait_for(ul, std::chrono::milliseconds(500), [&]{return got;});
  ASSERT_TRUE(ok) << "on_console callback not invoked";
  EXPECT_EQ(received, (B{'H','i'}));
}

// received HELLO CONTROL frame updates peer version
TEST(ConsoleClient, ReceiveHelloUpdatesVersion){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);

  // inject FT_MBOX: body = [CONTROL, HELLO, maj=2, min=7]
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 2, 7});

  // Poll rather than fix a sleep: the drain thread applies the version async.
  for(int i=0; i<200 && cc.peer_ver_maj()==0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_EQ(cc.peer_ver_maj(), 2);
  EXPECT_EQ(cc.peer_ver_min(), 7);
}

static void reply_after_stream(MockTransport& t, bool legacy = false){
  for(int i=0; i<1000; ++i){
    bool ready;
    {
      std::lock_guard<std::mutex> l(t.cmd_mutex());
      ready = count_ctrl(t, MBOX_CTRL_SET_STREAM) != 0;
      if(legacy) ready = ready && count_ctrl(t, MBOX_CTRL_HELLO) >= 2;
    }
    if(ready){
      // Simulate MAIN's 100ms idle poll. connect must wait for this reply.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if(legacy) t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 0});
      else t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, 1});
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

// connect must wait until MAIN has consumed SET_STREAM, not just HELLO.
TEST(ConsoleClient, ConnectEnablesStreamAfterHelloReply){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 1});
    reply_after_stream(t);
  });
  const auto started = std::chrono::steady_clock::now();
  bool ok = cc.connect(1000);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  responder.join();
  EXPECT_TRUE(ok);
  EXPECT_GE(elapsed, std::chrono::milliseconds(100));
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  // HELLO is sent first (a slow reply may cause >=1 retries); SET_STREAM is sent
  // exactly once and only after the reply. Assert by content, not by position.
  auto* h = find_cmd(t, OP_MBOX, 0);
  ASSERT_NE(h, nullptr);
  EXPECT_EQ(*h, (B{OP_MBOX, 0x02, MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO}));
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_HELLO), 1u);
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_SET_STREAM), 1u);
}

// (f) connect() returns false and does NOT enable the stream if no reply arrives
TEST(ConsoleClient, ConnectTimesOutWithoutReply){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  bool ok = cc.connect(50);   // no HELLO reply injected
  EXPECT_FALSE(ok);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_HELLO), 1u);   // HELLO was sent (>=1, may retry)
  EXPECT_EQ(count_ctrl(t, MBOX_CTRL_SET_STREAM), 0u); // SET_STREAM was NOT sent
}

// (g) connect() resends HELLO until the reply arrives: a HELLO lost in the
// one-deep inbox must not fail the session. Reply injected after the first
// retry interval; connect should still succeed with more than one HELLO sent.
TEST(ConsoleClient, ConnectRetriesHelloUntilReply){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 1});
    reply_after_stream(t);
  });
  bool ok = cc.connect(2000);
  responder.join();
  EXPECT_TRUE(ok);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_HELLO), 2u);      // at least one retry
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_SET_STREAM), 1u);
}

TEST(ConsoleClient, ConnectFailsWithoutStreamAcknowledgement){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 1});
  });
  const bool ok = cc.connect(250);
  responder.join();
  EXPECT_FALSE(ok);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  EXPECT_GE(count_ctrl(t, MBOX_CTRL_SET_STREAM), 2u);
}

TEST(ConsoleClient, ConnectUsesHelloBarrierForLegacyPeer){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 0});
    reply_after_stream(t, true);
  });
  const auto started = std::chrono::steady_clock::now();
  const bool ok = cc.connect(1000);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  responder.join();
  EXPECT_TRUE(ok);
  EXPECT_GE(elapsed, std::chrono::milliseconds(100));
}

TEST(ConsoleClient, ApiRequiresMatchingModeAcknowledgement){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 2});
    reply_after_stream(t); // ACK mode 1 must not complete API bring-up.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, 2});
  });
  const auto started = std::chrono::steady_clock::now();
  bool ok = cc.connect(1000, true);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  responder.join();
  EXPECT_TRUE(ok);
  EXPECT_GE(elapsed, std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  bool requestedApi = false;
  for(const auto& c : t.commands)
    if(c == B{OP_MBOX, 3, MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, 2}) requestedApi = true;
  EXPECT_TRUE(requestedApi);
}

TEST(ConsoleClient, ApiRejectsLegacyFirmware){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::thread responder([&]{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 1});
  });
  EXPECT_THROW(cc.connect(500, true), std::runtime_error);
  responder.join();
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  EXPECT_EQ(count_ctrl(t, MBOX_CTRL_SET_STREAM), 0u);
}

// (h) separate keystroke sends are paced too, not just chunks of one call: the
// global min-gap keeps each body off a still-full inbox. Lower-bound timing:
// 5 single-byte sends => at least 4 inter-send gaps of CHUNK_PACE_MS (15ms).
TEST(ConsoleClient, SendConsolePacesConsecutiveCalls){
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  const uint8_t bytes[] = {'1','2','3','4','5'};
  auto t0 = std::chrono::steady_clock::now();
  for(uint8_t b : bytes) cc.send_console(&b, 1);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  EXPECT_GE(elapsed, 50);   // 4 gaps * 15ms = 60ms minimum, allow scheduling slack
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  size_t mbox = 0;
  for(auto& c : t.commands)
    if(c.size()>=3 && c[0]==OP_MBOX && c[2]==MBOX_TYPE_CONSOLE) ++mbox;
  EXPECT_EQ(mbox, 5u);
}
