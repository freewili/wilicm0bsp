// notify_monitor - watch the FPGA's swap_ready NOTIFYs event-driven. Register a
// callback, open the router, and let the drain thread deliver notifications
// while the main loop does other work. Ctrl-C to stop.
//
// Build: cmake -S . -B build -DFWCM0_TARGET=ON -DFWCM0_EXAMPLES=ON
// Run on the CM0:
//   sudo ./build/ex_notify_monitor
#include "fwcm0/sram_router.h"
#include "fwcm0/linux_transport.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

using namespace fwcm0;

namespace {
std::atomic<bool>     g_run{true};
std::atomic<unsigned> g_swaps{0};
void on_sigint(int) { g_run = false; }
}

int main() {
  LinuxConfig cfg;
  LinuxTransport transport(cfg);
  SramRouter router(transport);

  std::signal(SIGINT, on_sigint);

  // The callback runs on the drain thread, so keep it short and use atomics
  // (or hand off to a queue) for anything the main thread also touches.
  router.on_swap_ready([]{
    unsigned n = ++g_swaps;
    std::printf("swap_ready #%u\n", n);
    std::fflush(stdout);
  });

  try {
    router.open();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }

  std::printf("watching for swap_ready notifies (Ctrl-C to stop)...\n");
  while (g_run) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::printf("\n%u swaps observed\n", g_swaps.load());
  return 0;
}
