#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace fwcm0 {

struct Status {
  bool init_done=false, boot_ready=false, quiesced=false, ready=false;
  bool wr_active=false, rd_active=false, abort=false;
  uint8_t  last_completed_seq=0;
  uint32_t bytes_written=0;
};

// One logical SPI command = CS asserted across one or more spi_write() calls,
// deasserted at end_command(). uart_read() blocks up to timeout_ms for bytes.
class Transport {
public:
  virtual ~Transport() = default;
  virtual void begin_command() = 0;                       // assert CS (+ settle)
  virtual void spi_write(const uint8_t* data, size_t n) = 0;
  virtual void end_command() = 0;                         // deassert CS
  virtual size_t uart_read(uint8_t* buf, size_t max, int timeout_ms) = 0; // 0 on timeout
};

} // namespace fwcm0
