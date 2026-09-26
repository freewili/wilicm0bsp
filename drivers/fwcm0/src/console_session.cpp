#include "fwcm0/console_session.h"
#include <chrono>
#include <thread>
#include <vector>
namespace fwcm0 {

int run_console_session(
    ConsoleClient& client,
    const std::function<long(uint8_t*, size_t)>& read_in,
    const std::function<void(const uint8_t*, size_t)>& write_out,
    bool interactive,
    int eof_grace_ms) {

  client.on_console([&](const std::vector<uint8_t>& body) {
    if (!body.empty()) write_out(body.data(), body.size());
  });
  struct Detach {
    ConsoleClient& client;
    ~Detach() { client.on_console(nullptr); }
  } detach{client};

  int rc = 0;
  uint8_t buf[64];
  for (;;) {
    long n = read_in(buf, sizeof(buf));
    if (n < 0) { rc = 1; break; }
    if (n == 0) {  // EOF
      if (eof_grace_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(eof_grace_ms));
      rc = 0; break;
    }
    size_t count = static_cast<size_t>(n);
    if (interactive) {
      size_t fwd = count;
      bool exit_now = false;
      for (size_t i = 0; i < count; ++i) if (buf[i] == 0x1D) { fwd = i; exit_now = true; break; }
      if (fwd > 0) client.send_console(buf, fwd);
      if (exit_now) { rc = 0; break; }
    } else {
      client.send_console(buf, count);
    }
  }

  return rc;
}

} // namespace fwcm0
