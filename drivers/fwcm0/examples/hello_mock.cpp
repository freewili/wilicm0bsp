// hello_mock - exercise the SramRouter API with no hardware, by driving it
// against a MockTransport that plays the FPGA. Builds host-side; swap the
// MockTransport for a LinuxTransport (see producer_loop.cpp) to talk to a
// real board.
//
// Build: cmake -S . -B build -DFWCM0_EXAMPLES=ON && cmake --build build
#include "fwcm0/sram_router.h"
#include "fwcm0/mock_transport.h"
#include "fwcm0/protocol.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace fwcm0;

int main() {
  MockTransport fpga;
  SramRouter router(fpga);

  // Stand in for the FPGA: answer each STATUS poll with boot_ready set, so
  // open()'s boot gate is satisfied. The real device drives these frames.
  std::atomic<bool> run{true};
  std::thread responder([&]{
    size_t next = 0;
    while (run) {
      {
        std::lock_guard<std::mutex> l(fpga.cmd_mutex());
        for (; next < fpga.commands.size(); ++next) {
          const auto& c = fpga.commands[next];
          if (!c.empty() && c[0] == OP_STATUS)
            fpga.inject_frame(FT_STATUS, c[1],
              {FLAG_INIT_DONE | FLAG_BOOT_READY, 0x00, 0, 0, 0, 0});
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  router.open();                       // blocks until a STATUS shows boot_ready
  Status s = router.status();
  std::printf("open ok: init_done=%d boot_ready=%d\n", s.init_done, s.boot_ready);

  run = false;
  responder.join();
  return 0;
}
