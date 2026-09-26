#pragma once
#include "fwcm0/transport.h"
#include "fwcm0/crc8.h"
#include "fwcm0/cobs.h"
#include "fwcm0/protocol.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <chrono>
namespace fwcm0 {

class MockTransport : public Transport {
public:
  // --- captured SPI ---
  std::vector<std::vector<uint8_t>> commands; // one entry per begin/end pair
  void begin_command() override { std::lock_guard<std::mutex> l(cmd_m_); cur_.clear(); in_cmd_=true; }
  void spi_write(const uint8_t* d, size_t n) override { std::lock_guard<std::mutex> l(cmd_m_); cur_.insert(cur_.end(), d, d+n); }
  void end_command() override { std::lock_guard<std::mutex> l(cmd_m_); commands.push_back(cur_); in_cmd_=false; }

  // --- injected UART ---
  void inject(const std::vector<uint8_t>& bytes){ std::lock_guard<std::mutex> l(m_); for(auto b:bytes) rx_.push_back(b); cv_.notify_all(); }

  // build + inject a byte-identical response frame (reuses the real codec)
  void inject_frame(uint8_t type, uint8_t seq, const std::vector<uint8_t>& payload){
    std::vector<uint8_t> pf{type, seq, static_cast<uint8_t>(payload.size()>>8), static_cast<uint8_t>(payload.size()&0xFF)};
    pf.insert(pf.end(), payload.begin(), payload.end());
    pf.push_back(crc8(pf.data(), pf.size()));
    auto w = cobs_encode(pf); w.push_back(0x00); inject(w);
  }

  size_t uart_read(uint8_t* buf, size_t max, int timeout_ms) override {
    std::unique_lock<std::mutex> l(m_);
    if (rx_.empty()) cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&]{return !rx_.empty()||stop_;});
    size_t k=0; while(k<max && !rx_.empty()){ buf[k++]=rx_.front(); rx_.pop_front(); } return k;
  }

  void stop(){ std::lock_guard<std::mutex> l(m_); stop_=true; cv_.notify_all(); }

  std::mutex& cmd_mutex(){ return cmd_m_; }

private:
  std::mutex m_; std::mutex cmd_m_; std::condition_variable cv_;
  std::deque<uint8_t> rx_; std::vector<uint8_t> cur_; bool in_cmd_=false, stop_=false;
};

} // namespace fwcm0
