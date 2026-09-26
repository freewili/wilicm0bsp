#include "fwcm0/mbox_mux.h"
#include "fwcm0/protocol.h"
#include <algorithm>
#include <thread>
namespace fwcm0 {

static constexpr int CHUNK_PACE_MS = 15;   // one FPGA inbox body deep; keep in sync with console
static constexpr size_t CHUNK_MAX = 31;    // CM0_MAX_MSG payload after the type byte

MboxMux::MboxMux(SramRouter& r) : router_(r) {
  router_.on_mbox_frame([this](const std::vector<uint8_t>& body){ dispatch(body); });
}

MboxMux::~MboxMux() {
  // Router drain thread outlives us and dispatches under mbox_mtx; clearing here
  // blocks any in-flight frame and prevents a use-after-free on captured this.
  router_.on_mbox_frame(nullptr);
}

void MboxMux::on_type(uint8_t type, Handler h) {
  std::lock_guard<std::mutex> l(map_mtx_); type_handlers_[type] = std::move(h);
}
void MboxMux::on_control(uint8_t subop, Handler h) {
  std::lock_guard<std::mutex> l(map_mtx_); control_handlers_[subop] = std::move(h);
}

void MboxMux::dispatch(const std::vector<uint8_t>& body) {
  if (body.empty()) return;
  // Invoke the handler while holding map_mtx_ so a concurrent on_type/on_control
  // (e.g. on_type(...,nullptr) from a client's destructor) blocks until an
  // in-flight handler returns. Mirrors SramRouter's dispatch-under-mbox_mtx and
  // is what lets a client be destroyed safely while the router RX thread runs.
  // Handlers must not re-register from within a dispatch (would self-deadlock);
  // none in this codebase do.
  std::lock_guard<std::mutex> l(map_mtx_);
  Handler h;
  if (body[0] == MBOX_TYPE_CONTROL) {
    if (body.size() < 2) return;
    auto it = control_handlers_.find(body[1]);
    if (it != control_handlers_.end()) h = it->second;
  } else {
    auto it = type_handlers_.find(body[0]);
    if (it != type_handlers_.end()) h = it->second;
  }
  if (h) h(body);
}

void MboxMux::send(const std::vector<uint8_t>& body) {
  using namespace std::chrono;
  std::lock_guard<std::mutex> l(send_mtx_);
  auto target = last_send_ + milliseconds(CHUNK_PACE_MS);
  auto now = steady_clock::now();
  if (now < target) std::this_thread::sleep_for(target - now);  // lock held: serialization IS the purpose
  router_.send_mbox(body);
  last_send_ = steady_clock::now();
}

void MboxMux::send_chunked(uint8_t type, const uint8_t* d, size_t n) {
  size_t off = 0;
  while (off < n) {
    size_t chunk = std::min(CHUNK_MAX, n - off);
    std::vector<uint8_t> body;
    body.reserve(1 + chunk);
    body.push_back(type);
    body.insert(body.end(), d + off, d + off + chunk);
    send(body);
    off += chunk;
  }
}

} // namespace fwcm0
