#include <gtest/gtest.h>
#include "fwcm0/mbox_mux.h"
#include "fwcm0/mock_transport.h"
#include "fwcm0/commands.h"
#include "fwcm0/protocol.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
using namespace fwcm0;
using B = std::vector<uint8_t>;

static const B* find_cmd(const MockTransport& t, uint8_t op, size_t n = 0) {
  size_t found = 0;
  for (auto& c : t.commands) if (!c.empty() && c[0] == op) { if (found == n) return &c; ++found; }
  return nullptr;
}

// A CONSOLE-type frame reaches the CONSOLE handler with the type byte intact.
TEST(MboxMux, DispatchesByType) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  std::mutex m; B got;
  mux.on_type(MBOX_TYPE_SHELL, [&](const B& body){ std::lock_guard<std::mutex> l(m); got = body; });
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_SHELL, 'x', 'y'});
  for (int i = 0; i < 100; ++i) { { std::lock_guard<std::mutex> l(m); if (!got.empty()) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  std::lock_guard<std::mutex> l(m);
  EXPECT_EQ(got, (B{MBOX_TYPE_SHELL, 'x', 'y'}));
}

// A CONTROL frame is routed by its sub-op byte, not the type byte.
TEST(MboxMux, DispatchesControlBySubop) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  std::mutex m; B got; int hits = 0;
  mux.on_control(MBOX_CTRL_SHELL_ATTACH, [&](const B& body){ std::lock_guard<std::mutex> l(m); got = body; ++hits; });
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONTROL, MBOX_CTRL_SHELL_ATTACH, 1});
  t.inject_frame(FT_MBOX, 0x01, {MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO, 1, 0}); // different subop, ignored
  for (int i = 0; i < 100; ++i) { { std::lock_guard<std::mutex> l(m); if (hits) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  std::lock_guard<std::mutex> l(m);
  EXPECT_EQ(hits, 1);
  EXPECT_EQ(got, (B{MBOX_TYPE_CONTROL, MBOX_CTRL_SHELL_ATTACH, 1}));
}

// send_chunked splits payloads >31 B and prefixes the type byte.
TEST(MboxMux, SendChunkedFramesAndSplits) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  B data(40, 'a');
  mux.send_chunked(MBOX_TYPE_SHELL, data.data(), data.size());
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c0 = find_cmd(t, OP_MBOX, 0);
  auto* c1 = find_cmd(t, OP_MBOX, 1);
  ASSERT_NE(c0, nullptr); ASSERT_NE(c1, nullptr);
  // OP_MBOX frame = {OP_MBOX, bodylen, MBOX_TYPE_SHELL, payload...}; first chunk = 31 payload bytes.
  EXPECT_EQ((*c0)[0], OP_MBOX);
  EXPECT_EQ((*c0)[2], MBOX_TYPE_SHELL);
  EXPECT_EQ((*c0).size(), size_t(2 + 1 + 31)); // op+len + type + 31
  EXPECT_EQ((*c1).size(), size_t(2 + 1 + 9));  // remaining 9
}

// Two sends are spaced by at least CHUNK_PACE_MS (the global one-deep-inbox pace).
TEST(MboxMux, SerializesSendsWithPacing) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  auto t0 = std::chrono::steady_clock::now();
  mux.send({MBOX_TYPE_SHELL, 'a'});
  mux.send({MBOX_TYPE_SHELL, 'b'});
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  EXPECT_GE(elapsed, 15 - 2); // >= CHUNK_PACE_MS, small scheduling slack
}

// Two CONCURRENT senders (different threads) are still serialized by the pace
// gap: send_mtx_ must be held across the sleep, not released before it, or two
// threads can both read the same stale last_send_ and fire back-to-back.
TEST(MboxMux, SerializesConcurrentSendersWithPacing) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  auto t0 = std::chrono::steady_clock::now();
  std::thread a([&]{ mux.send({MBOX_TYPE_SHELL, 'a'}); });
  std::thread b([&]{ mux.send({MBOX_TYPE_SHELL, 'b'}); });
  a.join(); b.join();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  EXPECT_GE(elapsed, 15 - 2);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  int mbox_frames = 0;
  for (auto& c : t.commands) if (!c.empty() && c[0] == OP_MBOX) ++mbox_frames;
  EXPECT_EQ(mbox_frames, 2);   // both sends landed, none dropped
}

// unregister (on_type(...,nullptr)) must block until an in-flight handler
// returns, matching SramRouter's dispatch-under-mbox_mtx guarantee. This is
// what lets a client whose handler captures `this` be destroyed safely while
// the router RX thread is mid-dispatch.
TEST(MboxMux, UnregisterBlocksUntilInFlightHandlerReturns) {
  MockTransport t; SramRouter r(t); MboxMux mux(r);
  std::atomic<bool> started{false}, done{false};
  mux.on_type(MBOX_TYPE_SHELL, [&](const std::vector<uint8_t>&){
    started = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    done = true;
  });
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_SHELL, 'x'});     // RX thread dispatches
  while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  mux.on_type(MBOX_TYPE_SHELL, nullptr);                     // must block until handler returns
  EXPECT_TRUE(done.load());                                  // false here would mean unregister didn't block
}
