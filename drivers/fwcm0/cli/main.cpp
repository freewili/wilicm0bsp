// fwcm0 - bench CLI over the CM0 SRAM router.
//
// usage:
//   fwcm0 status
//   fwcm0 read  <addr-hex> <len>
//   fwcm0 write <addr-hex> <hex-bytes | @file>
//   fwcm0 monitor          # print NOTIFYs until Ctrl-C
//   fwcm0 console          # interactive/scriptable session into MAIN's text menu
#include "fwcm0/sram_router.h"
#include "fwcm0/linux_transport.h"
#include "fwcm0/bridge_proto.h"
#include "fwcm0/router_access.h"
#include "console_cli.h"
#include "bridge_daemon.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <vector>

using namespace fwcm0;

namespace {

std::atomic<bool> g_run{true};
void on_sigint(int) { g_run = false; }

void usage() {
  std::fprintf(stderr,
    "usage:\n"
    "  fwcm0 status\n"
    "  fwcm0 read  <addr-hex> <len>\n"
    "  fwcm0 write <addr-hex> <hex-bytes | @file>\n"
    "  fwcm0 swap\n"
    "  fwcm0 monitor\n"
    "  fwcm0 console\n"
    "  fwcm0 api\n"
    "  fwcm0 bridge\n"
    "  fwcm0 help\n");
}

void help() {
  std::printf(
    "fwcm0 - bench CLI for the CM0 SRAM router (FPGA over SPI0 + UART0)\n"
    "\n"
    "commands:\n"
    "  status                 poll the router STATUS frame: init/boot/swap\n"
    "                         flags, last completed seq, bytes written\n"
    "  read <addr-hex> <len>  read <len> (decimal) bytes from <addr-hex>;\n"
    "                         large reads are chunked and stitched\n"
    "  write <addr-hex> <hex-bytes | @file>\n"
    "                         fire-and-forget write; bytes as hex pairs\n"
    "                         (\"DEADBEEF\") or a raw file (\"@data.bin\")\n"
    "  swap                   request a buffer swap (OP_SWAP_REQUEST) and\n"
    "                         block for the swap_ready notify, 10 s limit\n"
    "  monitor                print NOTIFYs until Ctrl-C\n"
    "  console                interactive/scriptable session into MAIN's text\n"
    "                         menu (fwMenuMain). On a TTY: a raw terminal,\n"
    "                         Ctrl-] to exit, Ctrl-C/Ctrl-F pass through to\n"
    "                         MAIN. Piped: a transparent byte stream for\n"
    "                         scripting (drive with pexpect/expect)\n"
    "  help                   this text\n"
    "  api                    isolated OneWili command stream (MAIN protocol 1.2+)\n"
    "\n"
    "devices (LinuxConfig defaults):\n"
    "  /dev/spidev0.0 at 7 MHz, /dev/ttyAMA0 at 7.8125 Mbaud with RTS/CTS,\n"
    "  software chip-select on /dev/gpiochip0 line 21\n"
    "\n"
    "environment:\n"
    "  FWCM0_SPI_HZ           override the SPI clock in Hz\n"
    "  FWCM0_BAUD             override the UART baud rate\n"
    "\n"
    "exit codes: 0 ok, 1 router or transport error, 2 usage\n"
    "\n"
    "Run under sudo or grant cap_sys_nice,cap_ipc_lock for the SCHED_FIFO\n"
    "drain thread; without it a warning is printed and the driver runs at\n"
    "normal priority.\n");
}

uint32_t parse_addr(const char* s) {
  return static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
}

// Parse "AABBCC" hex pairs, or "@path" to read raw file bytes.
std::vector<uint8_t> parse_data(const std::string& arg) {
  std::vector<uint8_t> out;
  if (!arg.empty() && arg[0] == '@') {
    std::ifstream f(arg.substr(1), std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", arg.c_str() + 1); std::exit(2); }
    char c;
    while (f.get(c)) out.push_back(static_cast<uint8_t>(c));
    return out;
  }
  std::string hex;
  for (char c : arg) if (!std::isspace(static_cast<unsigned char>(c))) hex.push_back(c);
  if (hex.size() % 2 != 0) { std::fprintf(stderr, "hex must be byte pairs\n"); std::exit(2); }
  for (size_t i = 0; i < hex.size(); i += 2)
    out.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
  return out;
}

void print_status(const Status& s) {
  std::printf("init_done=%d boot_ready=%d quiesced=%d ready=%d wr_active=%d rd_active=%d abort=%d "
              "last_seq=0x%02X bytes_written=%u\n",
              s.init_done, s.boot_ready, s.quiesced, s.ready, s.wr_active, s.rd_active, s.abort,
              s.last_completed_seq, s.bytes_written);
}

void print_hex(const std::vector<uint8_t>& d) {
  for (size_t i = 0; i < d.size(); ++i) {
    std::printf("%02X", d[i]);
    std::printf((i + 1) % 16 == 0 ? "\n" : " ");
  }
  if (d.size() % 16 != 0) std::printf("\n");
}

// Prefer a running bridge daemon (probed with a throwaway connect); fall back
// to owning the router directly when none is listening. A socket we are not
// allowed to connect to is NOT a fallback case: the daemon behind it owns the
// SPI/GPIO transport, so going direct just yields a confusing gpiod EBUSY.
std::unique_ptr<RouterAccess> make_router_access() {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd >= 0) {
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, fwcm0::bridge::DEFAULT_SOCK, sizeof(a.sun_path) - 1);
    bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    int err = errno;
    ::close(fd);
    if (connected) return std::make_unique<SocketRouterAccess>(fwcm0::bridge::DEFAULT_SOCK);
    if (err == EACCES || err == EPERM)
      throw std::runtime_error(std::string("bridge socket ") + fwcm0::bridge::DEFAULT_SOCK +
                               ": permission denied (needs group dialout); the bridge owns the "
                               "hardware, so direct access would fail busy");
  }
  return std::make_unique<DirectRouterAccess>();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  std::string cmd = argv[1];
  if (cmd == "help") { help(); return 0; }

  // console owns its own transport (bridge socket, or a direct SramRouter in
  // its fallback); it must not run alongside the shared transport/router
  // below, which would double-open the CS GPIO line.
  if (cmd == "console" || cmd == "api") {
    try {
      return fwcm0::run_console_cli(fwcm0::bridge::DEFAULT_SOCK, cmd == "api");
    } catch (const std::exception& e) {
      std::fprintf(stderr, "error: %s\n", e.what());
      return 1;
    }
  }

  // bridge owns the router itself (it's what SocketRouterAccess talks to);
  // it must not go through RouterAccess like the other subcommands below.
  if (cmd == "bridge") {
    try {
      LinuxConfig cfg;
      if (const char* s = std::getenv("FWCM0_SPI_HZ")) cfg.spi_hz = std::strtoul(s, nullptr, 10);
      if (const char* s = std::getenv("FWCM0_BAUD"))   cfg.baud   = std::strtoul(s, nullptr, 10);
      LinuxTransport transport(cfg);
      SramRouter router(transport);
      router.open();
      return run_bridge_daemon(router, fwcm0::bridge::DEFAULT_SOCK);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "error: %s\n", e.what());
      return 1;
    }
  }

  try {
    std::unique_ptr<RouterAccess> access = make_router_access();

    if (cmd == "status") {
      print_status(access->status());
    } else if (cmd == "read") {
      if (argc < 4) { usage(); return 2; }
      uint32_t addr = parse_addr(argv[2]);
      uint16_t len = static_cast<uint16_t>(std::strtoul(argv[3], nullptr, 10));
      print_hex(access->read(addr, len));
    } else if (cmd == "write") {
      if (argc < 4) { usage(); return 2; }
      uint32_t addr = parse_addr(argv[2]);
      std::vector<uint8_t> data = parse_data(argv[3]);
      access->write(addr, data);
      std::printf("wrote %zu bytes at 0x%06X\n", data.size(), addr);
    } else if (cmd == "swap") {
      if (access->swap(10000)) {
        std::printf("swap complete (swap_ready)\n");
      } else {
        std::fprintf(stderr, "swap requested; no swap_ready within 10 s\n");
        return 1;
      }
    } else if (cmd == "monitor") {
      std::signal(SIGINT, on_sigint);
      std::printf("monitoring (Ctrl-C to stop)...\n");
      access->monitor([]{ std::printf("NOTIFY: swap_ready\n"); std::fflush(stdout); }, g_run);
    } else {
      usage();
      return 2;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
