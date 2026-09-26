#include <gtest/gtest.h>
#include "fwcm0/console_session.h"
#include "fwcm0/mbox_mux.h"
#include "fwcm0/mock_transport.h"
#include "fwcm0/commands.h"
#include <atomic>
#include <chrono>
#include <deque>
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

// queue-backed read_in: returns queued bytes, then EOF
static std::function<long(uint8_t*, size_t)> queue_reader(std::shared_ptr<std::deque<uint8_t>> q) {
  return [q](uint8_t* b, size_t n) -> long {
    if (q->empty()) return 0;  // EOF
    size_t k = 0;
    while (k < n && !q->empty()) { b[k++] = q->front(); q->pop_front(); }
    return static_cast<long>(k);
  };
}

// input bytes are forwarded as a CONSOLE mailbox frame; EOF ends the loop
TEST(ConsoleSession, ForwardsInputThenEofStops) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  auto q = std::make_shared<std::deque<uint8_t>>(std::deque<uint8_t>{'h', 'i'});
  auto sink = [](const uint8_t*, size_t) {};
  int rc = run_console_session(cc, queue_reader(q), sink, /*interactive=*/false);
  EXPECT_EQ(rc, 0);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX, 0);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(*c, (B{OP_MBOX, 3, MBOX_TYPE_CONSOLE, 'h', 'i'}));
}

// Ctrl-] (0x1D) ends an interactive session; it and bytes after it are not forwarded
TEST(ConsoleSession, CtrlBracketExitsAndIsNotForwarded) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  auto q = std::make_shared<std::deque<uint8_t>>(std::deque<uint8_t>{'a', 'b', 0x1D, 'c'});
  auto sink = [](const uint8_t*, size_t) {};
  int rc = run_console_session(cc, queue_reader(q), sink, /*interactive=*/true);
  EXPECT_EQ(rc, 0);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c0 = find_cmd(t, OP_MBOX, 0);
  ASSERT_NE(c0, nullptr);
  EXPECT_EQ(*c0, (B{OP_MBOX, 3, MBOX_TYPE_CONSOLE, 'a', 'b'}));  // only "ab"
  EXPECT_EQ(find_cmd(t, OP_MBOX, 1), nullptr);                   // "c"/0x1D never sent
}

// inbound CONSOLE frames are written to the output sink while the session runs
TEST(ConsoleSession, DeliversInboundToWriteOut) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::mutex om; B out;
  auto sink = [&](const uint8_t* b, size_t n) { std::lock_guard<std::mutex> l(om); out.insert(out.end(), b, b + n); };
  std::atomic<bool> eof{false};
  auto blocking_reader = [&](uint8_t*, size_t) -> long {
    while (!eof.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return 0;  // EOF
  };
  std::thread sess([&] { run_console_session(cc, blocking_reader, sink, /*interactive=*/false); });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));  // let sess register on_console
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONSOLE, 'O', 'K'});
  for (int i = 0; i < 100; ++i) {
    { std::lock_guard<std::mutex> l(om); if (out.size() >= 2) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  eof = true; sess.join();
  std::lock_guard<std::mutex> l(om);
  EXPECT_EQ(out, (B{'O', 'K'}));
}

TEST(ConsoleSession, ExceptionDetachesOutputBeforeCallerUnwinds) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ConsoleClient cc(mux);
  std::atomic<unsigned> writes{0};
  auto sink = [&](const uint8_t*, size_t) { ++writes; };
  auto failing_reader = [](uint8_t*, size_t) -> long {
    throw std::runtime_error("connection failed");
  };
  EXPECT_THROW(run_console_session(cc, failing_reader, sink, false), std::runtime_error);

  // A following control frame is a barrier proving the preceding console
  // frame was dispatched. The failed session must no longer own a callback.
  std::mutex m;
  std::condition_variable cv;
  bool drained = false;
  mux.on_control(0x7f, [&](const B&) {
    std::lock_guard<std::mutex> lock(m);
    drained = true;
    cv.notify_one();
  });
  t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONSOLE, 'X'});
  t.inject_frame(FT_MBOX, 0, {MBOX_TYPE_CONTROL, 0x7f});
  {
    std::unique_lock<std::mutex> lock(m);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&] { return drained; }));
  }
  mux.on_control(0x7f, nullptr);
  EXPECT_EQ(writes.load(), 0u);
}
