#include "console_cli.h"
#include "bridge_frame.h"
#include "fwcm0/bridge_proto.h"
#include "fwcm0/linux_transport.h"
#include "fwcm0/sram_router.h"
#include "fwcm0/mbox_mux.h"
#include "fwcm0/console_client.h"
#include "fwcm0/console_session.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fwcm0 {
namespace {

// Raw-mode a TTY like the legacy console; restore on scope exit.
struct RawTty {
  bool raw = false; termios saved{};
  RawTty() {
    if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &saved) == 0) {
      termios t = saved;
      t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
      t.c_iflag &= ~(IXON | ICRNL);
      t.c_oflag |= (OPOST | ONLCR);
      t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
      ::tcsetattr(STDIN_FILENO, TCSANOW, &t);
      raw = true;
    }
  }
  ~RawTty() { if (raw) ::tcsetattr(STDIN_FILENO, TCSANOW, &saved); }
};

// -1 = no daemon listening (caller falls back to the direct path). A denied
// socket throws instead: the daemon behind it owns the SPI/GPIO transport, so
// the direct fallback would only produce a misleading gpiod EBUSY.
int try_connect(const char* path) {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_un a{}; a.sun_family = AF_UNIX; std::strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
  if (::connect(fd, (sockaddr*)&a, sizeof(a)) < 0) {
    int err = errno;
    ::close(fd);
    if (err == EACCES || err == EPERM)
      throw std::runtime_error(std::string("bridge socket ") + path +
                               ": permission denied (needs group dialout); the bridge owns the "
                               "hardware, so direct access would fail busy");
    return -1;
  }
  return fd;
}

// Socket path: send OP_CONSOLE, then pump stdin<->socket. Ctrl-] exits locally.
int run_over_socket(int sfd, bool api) {
  RawTty tty;
  if (!bridge::write_frame(sfd, {api ? bridge::OP_API : bridge::OP_CONSOLE})) { ::close(sfd); return 1; }
  std::fprintf(stderr, "[fwcm0 console] bridge session. Ctrl-] to exit.\r\n");

  // Reader thread: socket -> stdout.
  bool done = false; std::mutex dm;
  std::thread rx([&]{
    uint8_t b[256];
    for (;;) { ssize_t r = ::read(sfd, b, sizeof(b)); if (r <= 0) break;
      size_t off = 0; while (off < (size_t)r) { ssize_t w = ::write(STDOUT_FILENO, b + off, r - off); if (w <= 0) break; off += (size_t)w; } }
    std::lock_guard<std::mutex> l(dm); done = true;
  });
  // Main: stdin -> socket, break on Ctrl-] (0x1D).
  int rc = 0;
  uint8_t buf[64];
  for (;;) {
    { std::lock_guard<std::mutex> l(dm); if (done) break; }
    // Poll rather than park in read(): a socket the daemon closed (refused,
    // or MAIN went away) must end the session without waiting for a keypress.
    pollfd p{}; p.fd = STDIN_FILENO; p.events = POLLIN;
    int pr = ::poll(&p, 1, 200);
    if (pr < 0) { if (errno == EINTR) continue; rc = 1; break; }
    if (pr == 0) continue;                        // rechecks done at the loop top
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n < 0) { rc = 1; break; }
    if (n == 0) break;
    size_t fwd = (size_t)n; bool exit_now = false;
    for (ssize_t i = 0; i < n; ++i) if (buf[i] == 0x1D) { fwd = i; exit_now = true; break; }
    if (fwd > 0) { size_t off = 0; while (off < fwd) { ssize_t w = ::write(sfd, buf + off, fwd - off); if (w <= 0) break; off += (size_t)w; } }
    if (exit_now) break;
  }
  // Half-close input and wait for the daemon to detach the session and close
  // its end. This makes process exit a release barrier for immediate reopen.
  ::shutdown(sfd, SHUT_WR); rx.join(); ::close(sfd);
  return rc;
}

// Direct path (no daemon): own the router, exactly the legacy console behavior.
int run_direct(bool api) {
  // Declared first so they outlive transport/router/mux/client below: their
  // destructors run last, after ~ConsoleClient has blocked on any in-flight
  // RX-thread dispatch into write_out.
  std::mutex om;
  auto write_out = [&om](const uint8_t* b, size_t n) { std::lock_guard<std::mutex> l(om);
    size_t off = 0; while (off < n) { ssize_t w = ::write(STDOUT_FILENO, b + off, n - off); if (w <= 0) break; off += (size_t)w; } };

  LinuxConfig cfg;
  if (const char* s = std::getenv("FWCM0_SPI_HZ")) cfg.spi_hz = std::strtoul(s, nullptr, 10);
  if (const char* s = std::getenv("FWCM0_BAUD"))   cfg.baud   = std::strtoul(s, nullptr, 10);
  LinuxTransport transport(cfg); SramRouter router(transport); router.open();
  MboxMux mux(router); ConsoleClient client(mux);
  if (!client.connect(1000, api)) { std::fprintf(stderr, "console: MAIN did not complete mailbox handshake within %d ms\n", 1000); return 1; }
  RawTty tty;
  if (tty.raw) {
    std::fprintf(stderr, "[fwcm0 console] connected (peer %u.%u). Ctrl-] to exit.\r\n",
                 client.peer_ver_maj(), client.peer_ver_min());
  }
  const bool interactive = ::isatty(STDIN_FILENO) != 0;
  auto read_in = [](uint8_t* b, size_t n) -> long { return (long)::read(STDIN_FILENO, b, n); };
  int rc = run_console_session(client, read_in, write_out, interactive, interactive ? 0 : 500);
  client.set_stream(false);
  return rc;
}
} // namespace

int run_console_cli(const char* sock_path, bool api) {
  ::signal(SIGPIPE, SIG_IGN);
  int sfd = try_connect(sock_path);
  if (sfd >= 0) return run_over_socket(sfd, api);
  return run_direct(api);
}
} // namespace fwcm0
