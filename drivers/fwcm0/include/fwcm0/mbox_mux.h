#pragma once
#include "fwcm0/sram_router.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
namespace fwcm0 {

// Owns the router's single inbound mailbox callback (demux by type byte, and by
// sub-op for CONTROL) and the single paced CM0->MAIN sender. The FPGA inbox is
// one body deep, so every send is serialized here with a >= CHUNK_PACE_MS gap;
// console and shell share this one pacer so their frames cannot collide.
// Handlers run on the router RX thread; do not block in them.
class MboxMux {
public:
  using Handler = std::function<void(const std::vector<uint8_t>& body)>; // body includes type byte
  explicit MboxMux(SramRouter& r);
  ~MboxMux();

  void on_type(uint8_t type, Handler h);       // CONSOLE / SHELL
  void on_control(uint8_t subop, Handler h);   // HELLO / RESET / SHELL_ATTACH

  void send(const std::vector<uint8_t>& body);                     // one paced frame
  void send_chunked(uint8_t type, const uint8_t* d, size_t n);     // type byte + <=31 payload, paced

private:
  void dispatch(const std::vector<uint8_t>& body);

  SramRouter& router_;
  std::mutex send_mtx_;
  std::chrono::steady_clock::time_point last_send_{};
  std::mutex map_mtx_;
  std::map<uint8_t, Handler> type_handlers_;
  std::map<uint8_t, Handler> control_handlers_;
};

} // namespace fwcm0
