// Hardware bring-up test for the CM0<->MAIN console mailbox bridge.
// Enables output streaming, requests HELLO, nudges MAIN's fwMenuMain, and
// prints whatever console text MAIN streams back over the FPGA mailbox.
#include "fwcm0/sram_router.h"
#include "fwcm0/linux_transport.h"
#include "fwcm0/console_client.h"
#include "fwcm0/mbox_mux.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace fwcm0;

int main() {
  LinuxConfig cfg;                  // /dev/spidev0.0, /dev/ttyAMA0, gpiochip0:21
  cfg.realtime = false;             // no SCHED_FIFO: it starves the CM0 USB console
  // Match the FPGA link rate (the imager exports FWCM0_BAUD=7812500), like the CLI.
  if (const char* s = std::getenv("FWCM0_SPI_HZ")) cfg.spi_hz = std::strtoul(s, nullptr, 10);
  if (const char* s = std::getenv("FWCM0_BAUD"))   cfg.baud   = std::strtoul(s, nullptr, 10);
  std::printf("[console_test] spi_hz=%u baud=%u\n", cfg.spi_hz, cfg.baud);
  fflush(stdout);

  LinuxTransport transport(cfg);
  SramRouter router(transport);
  MboxMux mux(router);
  ConsoleClient client(mux);

  std::atomic<size_t> rx{0};
  client.on_console([&](const std::vector<uint8_t>& b) {
    fwrite(b.data(), 1, b.size(), stdout);
    fflush(stdout);
    rx.fetch_add(b.size());
  });

  router.open();
  std::printf("[console_test] router open; HELLO + enable stream\n");
  fflush(stdout);

  // Race-free bring-up: HELLO, wait for the reply, then enable the stream.
  bool connected = client.connect(1000);
  std::printf("[console_test] connect=%s peer HELLO version = %u.%u\n",
              connected ? "ok" : "TIMEOUT",
              client.peer_ver_maj(), client.peer_ver_min());
  fflush(stdout);

  std::printf("[console_test] sending CTRL-C + ENTER to the menu\n");
  std::printf("----- MAIN MENU OUTPUT -----\n");
  fflush(stdout);
  const uint8_t ctrlc = 0x03;
  client.send_console(&ctrlc, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const uint8_t cr = '\r';
  client.send_console(&cr, 1);

  for (int i = 0; i < 60; ++i)      // ~6 s
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::printf("\n----- END (%zu console bytes received) -----\n", rx.load());
  return 0;
}
