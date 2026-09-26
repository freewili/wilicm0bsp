#pragma once
#include "fwcm0/mbox_mux.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
namespace fwcm0 {

// CM0-side server for the TYPE_SHELL channel. MAIN drives it: SHELL_ATTACH spawns
// or kills bash; TYPE_SHELL bytes are keystrokes for the PTY. bash output goes
// back via send_shell. Callbacks run on the router RX thread; do not block.
// Attach is ref-counted on MAIN, so this sees exactly one enable and one disable.
class ShellServer {
public:
  explicit ShellServer(MboxMux& mux);
  ~ShellServer();

  void on_attach(std::function<void(bool enable)> cb);
  void on_shell_data(std::function<void(const uint8_t*, size_t)> cb);
  void send_shell(const uint8_t* data, size_t n);
  void send_shell_exit();   // bash ended without a detach; MAIN drops its ref count

private:
  MboxMux& mux_;
  std::mutex cb_mtx_;
  std::function<void(bool)> attach_cb_;
  std::function<void(const uint8_t*, size_t)> data_cb_;
};

} // namespace fwcm0
