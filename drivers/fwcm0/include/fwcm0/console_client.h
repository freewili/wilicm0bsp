#pragma once
#include "fwcm0/mbox_mux.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>
namespace fwcm0 {

// Sends and receives mailbox console messages through MboxMux.
// on_console callbacks run on the UART drain thread; do not block.
class ConsoleClient {
public:
  explicit ConsoleClient(MboxMux& mux);
  ~ConsoleClient();

  void set_stream(bool on);
  void hello();
  void reset();

  // Wait for HELLO, then for MAIN to acknowledge SET_STREAM (protocol 1.1).
  // Retry lost control messages within timeout_ms. Legacy 1.0 peers use a
  // second HELLO as a drain barrier after SET_STREAM. A successful return
  // means the single-body inbox is ready for the first console command.
  // api=true requires protocol 1.2 and isolates commands from USB console I/O.
  bool connect(int timeout_ms = 1000, bool api = false);

  // Chunks data into <=31-byte CONSOLE mailbox messages, paced by the mux so
  // each lands before the next.
  void send_console(const uint8_t* data, size_t n);

  // Register a callback for inbound CONSOLE bodies (type byte stripped).
  void on_console(std::function<void(const std::vector<uint8_t>&)> cb);

  // Received HELLO version (populated after the FPGA/MAIN sends HELLO back).
  uint8_t peer_ver_maj() const { return ver_maj_; }
  uint8_t peer_ver_min() const { return ver_min_; }

private:
  MboxMux& mux_;
  std::mutex cb_mtx_;                                       // guards console_cb_
  std::function<void(const std::vector<uint8_t>&)> console_cb_;
  std::atomic<uint8_t> ver_maj_{0}, ver_min_{0};
  std::atomic<bool> hello_seen_{false};   // set when MAIN's HELLO reply arrives
  std::atomic<int> stream_mode_{-1}; // last SET_STREAM ACK, -1 until received

  void handle_console_body(const std::vector<uint8_t>& body);  // MBOX_TYPE_CONSOLE
  void handle_hello(const std::vector<uint8_t>& body);         // MBOX_TYPE_CONTROL/HELLO
};

} // namespace fwcm0
