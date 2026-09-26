#include <gtest/gtest.h>
#include "fwcm0/shell_server.h"
#include "fwcm0/mbox_mux.h"
#include "fwcm0/mock_transport.h"
#include "fwcm0/commands.h"
#include "fwcm0/protocol.h"
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

TEST(ShellServer, AttachFrameFiresCallback) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ShellServer srv(mux);
  std::mutex m; int enables = 0; bool last = false;
  srv.on_attach([&](bool en){ std::lock_guard<std::mutex> l(m); ++enables; last = en; });
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_CONTROL, MBOX_CTRL_SHELL_ATTACH, 1});
  for (int i = 0; i < 100; ++i) { { std::lock_guard<std::mutex> l(m); if (enables) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  std::lock_guard<std::mutex> l(m);
  EXPECT_EQ(enables, 1);
  EXPECT_TRUE(last);
}

TEST(ShellServer, ShellDataReachesCallbackWithoutTypeByte) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ShellServer srv(mux);
  std::mutex m; B got;
  srv.on_shell_data([&](const uint8_t* d, size_t n){ std::lock_guard<std::mutex> l(m); got.assign(d, d + n); });
  t.inject_frame(FT_MBOX, 0x00, {MBOX_TYPE_SHELL, 'l', 's', 0x0A});
  for (int i = 0; i < 100; ++i) { { std::lock_guard<std::mutex> l(m); if (!got.empty()) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  std::lock_guard<std::mutex> l(m);
  EXPECT_EQ(got, (B{'l', 's', 0x0A}));
}

TEST(ShellServer, SendShellFramesWithTypeByte) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ShellServer srv(mux);
  const char* out = "hi";
  srv.send_shell(reinterpret_cast<const uint8_t*>(out), 2);
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  auto* c = find_cmd(t, OP_MBOX, 0);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(*c, (B{OP_MBOX, 3, MBOX_TYPE_SHELL, 'h', 'i'}));
}

TEST(ShellServer, SendShellChunksOver31Bytes) {
  MockTransport t; SramRouter r(t); MboxMux mux(r); ShellServer srv(mux);
  B big(40, 'z');
  srv.send_shell(big.data(), big.size());
  std::lock_guard<std::mutex> l(t.cmd_mutex());
  ASSERT_NE(find_cmd(t, OP_MBOX, 0), nullptr);
  ASSERT_NE(find_cmd(t, OP_MBOX, 1), nullptr);   // 40 -> 31 + 9
  EXPECT_EQ(find_cmd(t, OP_MBOX, 0)->size(), size_t(2 + 1 + 31));
  EXPECT_EQ(find_cmd(t, OP_MBOX, 1)->size(), size_t(2 + 1 + 9));
}
