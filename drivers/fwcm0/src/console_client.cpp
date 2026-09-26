#include "fwcm0/console_client.h"
#include "fwcm0/protocol.h"
#include <chrono>
#include <thread>
#include <stdexcept>
namespace fwcm0 {

static constexpr int HELLO_RETRY_MS = 100;

ConsoleClient::ConsoleClient(MboxMux& mux) : mux_(mux) {
  mux_.on_type(MBOX_TYPE_CONSOLE, [this](const std::vector<uint8_t>& b){ handle_console_body(b); });
  mux_.on_control(MBOX_CTRL_HELLO, [this](const std::vector<uint8_t>& b){ handle_hello(b); });
  mux_.on_control(MBOX_CTRL_SET_STREAM, [this](const std::vector<uint8_t>& b){
    if(b.size() >= 3) stream_mode_.store(b[2]);
  });
}

ConsoleClient::~ConsoleClient() {
  // Detach our handlers so an in-flight router frame can't call into a dead this.
  mux_.on_type(MBOX_TYPE_CONSOLE, nullptr);
  mux_.on_control(MBOX_CTRL_HELLO, nullptr);
  mux_.on_control(MBOX_CTRL_SET_STREAM, nullptr);
}

void ConsoleClient::set_stream(bool on){
  mux_.send({MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, static_cast<uint8_t>(on ? 1 : 0)});
}
void ConsoleClient::hello(){ mux_.send({MBOX_TYPE_CONTROL, MBOX_CTRL_HELLO}); }
void ConsoleClient::reset(){ mux_.send({MBOX_TYPE_CONTROL, MBOX_CTRL_RESET}); }

bool ConsoleClient::connect(int timeout_ms, bool api){
  using namespace std::chrono;
  hello_seen_.store(false);
  const auto deadline = steady_clock::now() + milliseconds(timeout_ms);
  while(!hello_seen_.load() && steady_clock::now() < deadline){
    hello();
    const auto next = steady_clock::now() + milliseconds(HELLO_RETRY_MS);
    while(!hello_seen_.load() && steady_clock::now() < next && steady_clock::now() < deadline)
      std::this_thread::sleep_for(milliseconds(5));
  }
  if(!hello_seen_.load()) return false;
  if(api && (ver_maj_.load() < 1 || (ver_maj_.load() == 1 && ver_min_.load() < 2)))
    throw std::runtime_error("OneWili API requires MAIN mailbox protocol 1.2; update MAIN firmware");
  stream_mode_.store(-1);
  const uint8_t mode = api ? 2 : 1;
  if(ver_maj_.load() > 1 || (ver_maj_.load() == 1 && ver_min_.load() >= 1)) {
    while(stream_mode_.load() != mode && steady_clock::now() < deadline){
      mux_.send({MBOX_TYPE_CONTROL, MBOX_CTRL_SET_STREAM, mode});
      const auto next = steady_clock::now() + milliseconds(HELLO_RETRY_MS);
      while(stream_mode_.load() != mode && steady_clock::now() < next && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds(5));
    }
    return stream_mode_.load() == mode;
  }
  // Older MAIN does not ACK SET_STREAM. A subsequent HELLO round trip proves
  // that it drained the preceding body; HELLO can be dropped while that body
  // still occupies the inbox, so retry until MAIN answers.
  set_stream(true);
  hello_seen_.store(false);
  while(!hello_seen_.load() && steady_clock::now() < deadline){
    hello();
    const auto next = steady_clock::now() + milliseconds(HELLO_RETRY_MS);
    while(!hello_seen_.load() && steady_clock::now() < next && steady_clock::now() < deadline)
      std::this_thread::sleep_for(milliseconds(5));
  }
  return hello_seen_.load();
}

void ConsoleClient::send_console(const uint8_t* data, size_t n){
  mux_.send_chunked(MBOX_TYPE_CONSOLE, data, n);
}

void ConsoleClient::on_console(std::function<void(const std::vector<uint8_t>&)> cb){
  std::lock_guard<std::mutex> l(cb_mtx_); console_cb_ = std::move(cb);
}

void ConsoleClient::handle_console_body(const std::vector<uint8_t>& body){
  // Invoke while holding cb_mtx_ (not released beforehand) so on_console(nullptr)
  // (which also takes cb_mtx_) blocks until an in-flight callback returns,
  // making it a true barrier: the caller can then safely destroy whatever the
  // callback captured. Mirrors MboxMux::dispatch's handler-under-lock shape.
  std::lock_guard<std::mutex> l(cb_mtx_);
  std::function<void(const std::vector<uint8_t>&)> cb = console_cb_;
  if(cb){ std::vector<uint8_t> payload(body.begin()+1, body.end()); cb(payload); }
}

void ConsoleClient::handle_hello(const std::vector<uint8_t>& body){
  if(body.size() >= 4){ ver_maj_ = body[2]; ver_min_ = body[3]; hello_seen_.store(true); }
}

} // namespace fwcm0
