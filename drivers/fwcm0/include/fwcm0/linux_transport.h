#pragma once
#include "fwcm0/transport.h"
#include <string>
struct gpiod_chip;
struct gpiod_line_request;
namespace fwcm0 {
struct LinuxConfig {
  std::string spidev   = "/dev/spidev0.0";
  std::string uart     = "/dev/ttyAMA0";
  std::string gpiochip = "/dev/gpiochip0";
  unsigned    cs_line  = 21;          // GPIO21 software CS
  uint32_t    spi_hz   = 7000000;     // <= ~7.5 MHz
  uint32_t    baud     = 7812500;     // FPGA cm0_uart G_BAUD_RATE (FWCM0_BAUD overrides)
  bool        realtime = true;        // SCHED_FIFO drain priority + mlockall
};
class LinuxTransport : public Transport {
public:
  explicit LinuxTransport(const LinuxConfig& cfg);
  ~LinuxTransport() override;
  void begin_command() override;
  void spi_write(const uint8_t* data, size_t n) override;
  void end_command() override;
  size_t uart_read(uint8_t* buf, size_t max, int timeout_ms) override;
  // mlockall + SCHED_FIFO on the calling thread; best effort, non-fatal on EPERM.
  void apply_realtime();
private:
  int spi_fd_ = -1, uart_fd_ = -1;
  gpiod_chip* chip_ = nullptr;
  gpiod_line_request* cs_req_ = nullptr;
  LinuxConfig cfg_;
};
}
