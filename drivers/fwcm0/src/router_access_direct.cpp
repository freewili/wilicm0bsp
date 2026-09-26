#include "fwcm0/router_access.h"
#include "fwcm0/linux_transport.h"
#include "fwcm0/sram_router.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace fwcm0 {
namespace {
// Same env-var knobs main.cpp used to read directly (FWCM0_SPI_HZ/FWCM0_BAUD)
// before the register subcommands moved behind RouterAccess.
LinuxConfig make_cfg() {
  LinuxConfig cfg;
  if (const char* s = std::getenv("FWCM0_SPI_HZ")) cfg.spi_hz = std::strtoul(s, nullptr, 10);
  if (const char* s = std::getenv("FWCM0_BAUD"))   cfg.baud   = std::strtoul(s, nullptr, 10);
  return cfg;
}
} // namespace

struct DirectRouterAccess::Impl {
  LinuxTransport transport;
  SramRouter router;
  Impl() : transport(make_cfg()), router(transport) { router.open(); }
};

DirectRouterAccess::DirectRouterAccess() : p_(new Impl()) {}
DirectRouterAccess::~DirectRouterAccess() { delete p_; }

Status DirectRouterAccess::status() { return p_->router.status(); }

std::vector<uint8_t> DirectRouterAccess::read(uint32_t addr, uint16_t len) {
  return p_->router.read(addr, len);
}

void DirectRouterAccess::write(uint32_t addr, const std::vector<uint8_t>& data) {
  p_->router.write(addr, data);
}

bool DirectRouterAccess::swap(int timeout_ms) {
  return p_->router.request_swap_and_wait(timeout_ms);
}

void DirectRouterAccess::monitor(const std::function<void()>& on_swap_ready, const std::atomic<bool>& run) {
  p_->router.on_swap_ready(on_swap_ready);
  while (run) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  p_->router.on_swap_ready(nullptr);
}

} // namespace fwcm0
