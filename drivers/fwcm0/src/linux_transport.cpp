#include "fwcm0/linux_transport.h"

#include <gpiod.h>
#include <linux/spi/spidev.h>
// termios2 + BOTHER for an arbitrary baud rate. asm/termbits.h defines struct
// termios2 and the c_cflag/c_iflag/c_lflag constants and must NOT be combined
// with <termios.h> (duplicate definitions). TCGETS2/TCSETS2 come in via ioctl.h.
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace fwcm0 {

static void die(const std::string& what) {
  throw std::runtime_error("LinuxTransport: " + what + ": " + std::strerror(errno));
}

LinuxTransport::LinuxTransport(const LinuxConfig& cfg) : cfg_(cfg) {
  // --- SPI ---
  spi_fd_ = ::open(cfg_.spidev.c_str(), O_RDWR);
  if (spi_fd_ < 0) die("open " + cfg_.spidev);
  uint8_t mode = SPI_MODE_0;
  if (::ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0) die("SPI_IOC_WR_MODE");
  uint8_t bits = 8;
  if (::ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) die("SPI_IOC_WR_BITS_PER_WORD");
  uint32_t hz = cfg_.spi_hz;
  if (::ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) die("SPI_IOC_WR_MAX_SPEED_HZ");
  uint32_t rd_hz = 0;
  if (::ioctl(spi_fd_, SPI_IOC_RD_MAX_SPEED_HZ, &rd_hz) < 0) die("SPI_IOC_RD_MAX_SPEED_HZ");
  if (rd_hz > cfg_.spi_hz)
    throw std::runtime_error("LinuxTransport: SPI clock " + std::to_string(rd_hz) +
                             " exceeds requested " + std::to_string(cfg_.spi_hz));

  // --- CS via libgpiod v2 (software CS, idle high) ---
  // Line is not flagged active-low, so ACTIVE = physical high = CS deasserted.
  chip_ = gpiod_chip_open(cfg_.gpiochip.c_str());
  if (!chip_) die("gpiod_chip_open " + cfg_.gpiochip);
  gpiod_line_settings*  settings = gpiod_line_settings_new();
  gpiod_line_config*    line_cfg = gpiod_line_config_new();
  gpiod_request_config* req_cfg  = gpiod_request_config_new();
  bool cfg_ok =
      settings && line_cfg && req_cfg &&
      gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) == 0 &&
      gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_ACTIVE) == 0 &&
      gpiod_line_config_add_line_settings(line_cfg, &cfg_.cs_line, 1, settings) == 0;
  if (cfg_ok) {
    gpiod_request_config_set_consumer(req_cfg, "fwcm0");
    cs_req_ = gpiod_chip_request_lines(chip_, req_cfg, line_cfg);
  }
  if (req_cfg)  gpiod_request_config_free(req_cfg);
  if (line_cfg) gpiod_line_config_free(line_cfg);
  if (settings) gpiod_line_settings_free(settings);
  if (!cs_req_) die(cfg_ok ? "gpiod_chip_request_lines" : "gpiod line config");

  // --- UART (termios2, raw, hardware flow control, arbitrary baud) ---
  uart_fd_ = ::open(cfg_.uart.c_str(), O_RDWR | O_NOCTTY);
  if (uart_fd_ < 0) die("open " + cfg_.uart);
  struct termios2 tio;
  std::memset(&tio, 0, sizeof(tio));
  if (::ioctl(uart_fd_, TCGETS2, &tio) < 0) die("TCGETS2");
  tio.c_cflag &= ~CBAUD;
  tio.c_cflag |= BOTHER | CLOCAL | CREAD | CRTSCTS;
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;
  tio.c_cflag &= ~PARENB;          // no parity
  tio.c_cflag &= ~CSTOPB;          // 1 stop bit
  tio.c_iflag &= ~(IXON | IXOFF | IXANY);                 // no software flow control
  tio.c_iflag &= ~(ICRNL | INLCR | IGNCR | ISTRIP | INPCK | BRKINT | IGNBRK | PARMRK);
  tio.c_oflag &= ~OPOST;                                  // raw output
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
  tio.c_ispeed = cfg_.baud;
  tio.c_ospeed = cfg_.baud;
  tio.c_cc[VMIN]  = 0;             // poll() governs blocking; non-canonical
  tio.c_cc[VTIME] = 0;
  if (::ioctl(uart_fd_, TCSETS2, &tio) < 0) die("TCSETS2");

  if (cfg_.realtime) apply_realtime();
}

LinuxTransport::~LinuxTransport() {
  if (cs_req_) gpiod_line_request_release(cs_req_);
  if (chip_) gpiod_chip_close(chip_);
  if (spi_fd_ >= 0) ::close(spi_fd_);
  if (uart_fd_ >= 0) ::close(uart_fd_);
}

void LinuxTransport::apply_realtime() {
  struct sched_param sp;
  std::memset(&sp, 0, sizeof(sp));
  sp.sched_priority = 50;
  bool rt = ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &sp) == 0;
  if (!rt)
    std::fprintf(stderr, "LinuxTransport: SCHED_FIFO failed (needs privilege); continuing\n");
  // Unprivileged MCL_FUTURE poisons later thread-stack mmaps (pthread_create
  // hits RLIMIT_MEMLOCK, fails EAGAIN), so only lock future pages when
  // privileged enough for SCHED_FIFO.
  if (::mlockall(rt ? MCL_CURRENT | MCL_FUTURE : MCL_CURRENT) < 0)
    std::fprintf(stderr, "LinuxTransport: mlockall failed (%s); continuing\n", std::strerror(errno));
}

void LinuxTransport::begin_command() {
  gpiod_line_request_set_value(cs_req_, cfg_.cs_line, GPIOD_LINE_VALUE_INACTIVE);  // assert CS (active low)
  ::usleep(5);                     // settle
}

void LinuxTransport::spi_write(const uint8_t* data, size_t n) {
  struct spi_ioc_transfer tr;
  std::memset(&tr, 0, sizeof(tr));
  tr.tx_buf = reinterpret_cast<unsigned long>(data);
  tr.rx_buf = 0;
  tr.len = static_cast<uint32_t>(n);
  tr.speed_hz = cfg_.spi_hz;
  tr.bits_per_word = 8;
  if (::ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &tr) < 0) die("SPI_IOC_MESSAGE");
}

void LinuxTransport::end_command() {
  gpiod_line_request_set_value(cs_req_, cfg_.cs_line, GPIOD_LINE_VALUE_ACTIVE);    // deassert CS
}

size_t LinuxTransport::uart_read(uint8_t* buf, size_t max, int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = uart_fd_;
  pfd.events = POLLIN;
  pfd.revents = 0;
  int rc = ::poll(&pfd, 1, timeout_ms);
  if (rc <= 0) return 0;                     // timeout or error -> no bytes
  if (!(pfd.revents & POLLIN)) return 0;
  ssize_t got = ::read(uart_fd_, buf, max);
  if (got <= 0) return 0;
  return static_cast<size_t>(got);
}

}
