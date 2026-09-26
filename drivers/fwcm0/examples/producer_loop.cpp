// producer_loop - the canonical CM0 producer pattern. Fill a buffer, write it
// to the FPGA SRAM, request a swap, and block until the swap completes before
// reusing the buffer. This is what the double-buffer swap path exists for: the
// CM0 produces into one buffer while the consumer reads the other.
//
// Build: cmake -S . -B build -DFWCM0_TARGET=ON -DFWCM0_EXAMPLES=ON
// Run on the CM0 (SPI0/UART0 enabled, see docs/cm0-pi-setup.md):
//   sudo ./build/ex_producer_loop
#include "fwcm0/sram_router.h"
#include "fwcm0/linux_transport.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace fwcm0;

int main() {
  LinuxConfig cfg;                     // spidev0.0, ttyAMA0, CS gpiochip0:21.
  // cfg.spi_hz / cfg.baud are tunable here to match the FPGA generics.
  LinuxTransport transport(cfg);
  SramRouter router(transport);

  const uint32_t buf_addr = 0x000000;
  const size_t   buf_len  = 4096;
  std::vector<uint8_t> buf(buf_len);

  try {
    router.open();                     // start the drain thread, wait for boot_ready

    for (int frame = 0; frame < 16; ++frame) {
      // Produce a frame of data (here, a rolling pattern).
      for (size_t i = 0; i < buf_len; ++i)
        buf[i] = static_cast<uint8_t>(frame + i);

      router.write(buf_addr, buf);     // fire-and-forget, chunked over SPI0

      // Hand the buffer to the consumer and wait for the swap to finish.
      // request_swap_and_wait blocks on the swap_ready NOTIFY, so the next
      // iteration never overwrites the buffer the consumer is reading.
      if (!router.request_swap_and_wait()) {
        std::fprintf(stderr, "frame %d: swap timed out\n", frame);
        return 1;
      }
      std::printf("frame %d: %zu bytes produced and swapped\n", frame, buf_len);
    }
  } catch (const NakError& e) {
    std::fprintf(stderr, "router NAK on seq %u (reason 0x%02X)\n", e.seq, e.reason);
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
