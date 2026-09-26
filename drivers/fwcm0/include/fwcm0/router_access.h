#pragma once
// Uniform register-op access, hiding whether a bridge daemon is present.
// DirectRouterAccess owns a real SramRouter (used when no daemon is running);
// SocketRouterAccess relays every call over the AF_UNIX bridge socket using
// the length-prefixed frame protocol (see src/bridge_frame.h). Target-only
// (both impls need LinuxTransport or AF_UNIX); this header itself is
// hardware/socket-free so it's safe to include anywhere.
#include "fwcm0/transport.h"   // Status
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>
namespace fwcm0 {

struct RouterAccess {
  virtual ~RouterAccess() = default;
  virtual Status status() = 0;
  virtual std::vector<uint8_t> read(uint32_t addr, uint16_t len) = 0;
  virtual void write(uint32_t addr, const std::vector<uint8_t>& data) = 0;
  virtual bool swap(int timeout_ms) = 0;
  // Registers on_swap_ready to run once per swap-ready notify, blocking until
  // run becomes false (or, for SocketRouterAccess, the daemon connection
  // drops).
  virtual void monitor(const std::function<void()>& on_swap_ready, const std::atomic<bool>& run) = 0;
};

// Opens and owns a real SramRouter (used when no daemon is running).
struct DirectRouterAccess : RouterAccess {
  DirectRouterAccess();
  ~DirectRouterAccess() override;
  Status status() override;
  std::vector<uint8_t> read(uint32_t addr, uint16_t len) override;
  void write(uint32_t addr, const std::vector<uint8_t>& data) override;
  bool swap(int timeout_ms) override;
  void monitor(const std::function<void()>& on_swap_ready, const std::atomic<bool>& run) override;
  struct Impl; Impl* p_;
};

// Talks to the bridge daemon over AF_UNIX; each call is a short-lived
// connect -> write request frame -> read response frame -> decode -> close.
struct SocketRouterAccess : RouterAccess {
  explicit SocketRouterAccess(const char* sock_path);
  Status status() override;
  std::vector<uint8_t> read(uint32_t addr, uint16_t len) override;
  void write(uint32_t addr, const std::vector<uint8_t>& data) override;
  bool swap(int timeout_ms) override;
  void monitor(const std::function<void()>& on_swap_ready, const std::atomic<bool>& run) override;
  const char* sock_;
};

} // namespace fwcm0
