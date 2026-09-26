#pragma once
#include "fwcm0/transport.h"
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <functional>
#include <memory>
#include <mutex>
namespace fwcm0 {
struct TimeoutError : std::runtime_error { TimeoutError():std::runtime_error("router timeout"){} };
struct NakError : std::runtime_error { uint8_t seq, reason; NakError(uint8_t s,uint8_t r):std::runtime_error("router NAK"),seq(s),reason(r){} };
class SramRouter {
public:
  explicit SramRouter(Transport& t);
  ~SramRouter();
  void open(int boot_timeout_ms = 2000);                       // start drain + boot gate
  void write(uint32_t addr, const std::vector<uint8_t>& data); // fire-and-forget, chunked
  std::vector<uint8_t> read(uint32_t addr, uint16_t len, int timeout_ms = 1000);
  Status status(int timeout_ms = 500);
  bool is_write_idle();                                        // STATUS wr_active clear
  void request_swap();                                         // fire-and-forget OP_SWAP_REQUEST
  // request_swap + block for the swap_ready NOTIFY so the caller cannot
  // write the about-to-be-swapped buffer. False on timeout.
  bool request_swap_and_wait(int timeout_ms = 5000);
  void on_swap_ready(std::function<void()> cb);
  void send_mbox(const std::vector<uint8_t>& body);
  // Register the inbound mailbox-frame handler (one only; re-registering replaces it).
  void on_mbox_frame(std::function<void(const std::vector<uint8_t>&)> cb);
  std::mutex& cmd_mutex_for_test();
private:
  struct Impl; std::unique_ptr<Impl> p_;
};
}
