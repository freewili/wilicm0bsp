#include "fwcm0/sram_router.h"
#include "fwcm0/commands.h"
#include "fwcm0/protocol.h"
#include "uart_receiver.h"
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <algorithm>
namespace fwcm0 {
struct Waiter { std::mutex m; std::condition_variable cv; bool done=false; Frame frame; bool nak=false; uint8_t nak_reason=0; };
struct SramRouter::Impl {
  Transport& t; std::mutex issue;                       // serializes command issue + seq
  uint8_t next_seq=1; std::map<uint8_t, Waiter*> waiters; std::mutex wmtx;
  std::function<void()> swap_cb; std::unique_ptr<UartReceiver> rx;
  std::mutex swap_mtx; std::condition_variable swap_cv; bool swap_seen=false;
  std::function<void(const std::vector<uint8_t>&)> mbox_cb; std::mutex mbox_mtx;
  Impl(Transport& tr):t(tr){}
  void on_frame(const Frame& f){
    if (f.type==FT_MBOX){
      // Dispatch under the lock so a concurrent on_mbox_frame(nullptr) (e.g. from
      // ~ConsoleClient) blocks until this returns and cannot detach the handler
      // mid-call, which would use-after-free the captured receiver.
      std::lock_guard<std::mutex> l(mbox_mtx);
      if(mbox_cb) mbox_cb(f.payload);
      return;
    }
    if (f.type==FT_NOTIFY){
      if(!f.payload.empty()&&f.payload[0]==NS_SWAP_READY){
        // Dispatch under the lock so a concurrent on_swap_ready(nullptr) (e.g.
        // from serve_monitor teardown) blocks until this returns and cannot
        // detach the handler mid-call, which would use-after-free the
        // captured client fd/mutex (mirrors the mbox branch above).
        std::lock_guard<std::mutex> l(swap_mtx);
        swap_seen=true;
        swap_cv.notify_all();
        if(swap_cb) swap_cb();
      }
      return;
    }
    std::lock_guard<std::mutex> l(wmtx); auto it=waiters.find(f.seq);
    if(it==waiters.end()) return;
    Waiter* w=it->second;
    std::lock_guard<std::mutex> wl(w->m);
    if(f.type==FT_NAK){ w->nak=true; w->nak_reason = f.payload.empty()?0:f.payload[0]; }
    else { w->frame=f; }
    w->done=true; w->cv.notify_one();
  }
  uint8_t alloc_seq(){ uint8_t s=next_seq++; if(next_seq==0) next_seq=1; return s; }
  // send a command that expects a reply with `seq`; block for it.
  Frame transact(uint8_t seq, const std::vector<uint8_t>& cmd, int timeout_ms){
    Waiter w; { std::lock_guard<std::mutex> l(wmtx); waiters[seq]=&w; }
    { std::lock_guard<std::mutex> l(issue); t.begin_command(); t.spi_write(cmd.data(), cmd.size()); t.end_command(); }
    std::unique_lock<std::mutex> wl(w.m);
    bool ok = w.cv.wait_for(wl, std::chrono::milliseconds(timeout_ms), [&]{return w.done;});
    { std::lock_guard<std::mutex> l(wmtx); waiters.erase(seq); }
    if(!ok) throw TimeoutError();
    if(w.nak) throw NakError(seq, w.nak_reason);
    return w.frame;
  }
};
SramRouter::SramRouter(Transport& t):p_(new Impl(t)){
  p_->rx.reset(new UartReceiver(t, [this](const Frame& f){ p_->on_frame(f); }));
  p_->rx->start();
}
SramRouter::~SramRouter(){ p_->rx->stop(); }
static Status parse_status(const Frame& f){
  Status s{}; if(f.payload.size()<6) return s; uint8_t fl=f.payload[0];
  s.init_done    = (fl & FLAG_INIT_DONE)   != 0;
  s.boot_ready   = (fl & FLAG_BOOT_READY)  != 0;
  s.quiesced     = (fl & FLAG_QUIESCED)    != 0;
  s.ready        = (fl & FLAG_READY)       != 0;
  s.wr_active    = (fl & FLAG_WR_ACTIVE)   != 0;
  s.rd_active    = (fl & FLAG_RD_ACTIVE)   != 0;
  s.abort        = (fl & FLAG_ABORT)       != 0;
  s.last_completed_seq=f.payload[1];
  s.bytes_written=(uint32_t(f.payload[2])<<24)|(uint32_t(f.payload[3])<<16)|(uint32_t(f.payload[4])<<8)|f.payload[5];
  return s;
}
Status SramRouter::status(int timeout_ms){ uint8_t s=p_->alloc_seq(); return parse_status(p_->transact(s, cmd_status(s), timeout_ms)); }
bool SramRouter::is_write_idle(){ return !status().wr_active; }
std::vector<uint8_t> SramRouter::read(uint32_t addr, uint16_t len, int timeout_ms){
  const uint16_t READ_CHUNK = 4096;                 // cap bytes requested per READ frame
  std::vector<uint8_t> out;
  uint32_t remaining = len;
  bool first = true;
  while (remaining > 0) {
    uint16_t n = static_cast<uint16_t>(remaining < READ_CHUNK ? remaining : READ_CHUNK);
    uint8_t s = p_->alloc_seq();
    Frame f = first ? p_->transact(s, cmd_read_at(s, addr, n), timeout_ms)
                    : p_->transact(s, cmd_read_cont(s, n), timeout_ms);
    out.insert(out.end(), f.payload.begin(), f.payload.end());
    uint32_t got = static_cast<uint32_t>(f.payload.size());
    if (got == 0) break;                 // no progress: stop rather than loop forever
    remaining -= (got > remaining ? remaining : got);
    first = false;
  }
  return out;
}
void SramRouter::write(uint32_t addr, const std::vector<uint8_t>& data){
  const size_t CHUNK=4000;                           // under spidev bufsiz 4096, leave header room
  std::lock_guard<std::mutex> l(p_->issue);
  p_->t.begin_command();
  size_t off=0; bool first=true;
  while(off<data.size()){
    size_t n=std::min(CHUNK, data.size()-off);
    std::vector<uint8_t> chunk(data.begin()+off, data.begin()+off+n);
    auto c = first ? cmd_write_at(addr, chunk) : cmd_write_cont(chunk);
    p_->t.spi_write(c.data(), c.size()); first=false; off+=n;
  }
  p_->t.end_command();
}
void SramRouter::open(int boot_timeout_ms){
  auto t0=std::chrono::steady_clock::now();
  while(true){
    // A lost or garbled exchange (stale half-frame from a prior session,
    // marginal first transfer) is retried until the gate expires.
    try { Status s=status(); if(s.boot_ready) return; }
    catch(const TimeoutError&){}
    if(std::chrono::steady_clock::now()-t0 > std::chrono::milliseconds(boot_timeout_ms)) throw TimeoutError();
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
}
void SramRouter::request_swap(){
  std::lock_guard<std::mutex> l(p_->issue);
  auto c = cmd_swap_request();
  p_->t.begin_command();
  p_->t.spi_write(c.data(), c.size());
  p_->t.end_command();
}
bool SramRouter::request_swap_and_wait(int timeout_ms){
  { std::lock_guard<std::mutex> l(p_->swap_mtx); p_->swap_seen=false; }
  request_swap();
  std::unique_lock<std::mutex> l(p_->swap_mtx);
  return p_->swap_cv.wait_for(l, std::chrono::milliseconds(timeout_ms), [&]{return p_->swap_seen;});
}
void SramRouter::on_swap_ready(std::function<void()> cb){ std::lock_guard<std::mutex> l(p_->swap_mtx); p_->swap_cb=std::move(cb); }
void SramRouter::send_mbox(const std::vector<uint8_t>& body){
  auto c = cmd_mbox(body);
  std::lock_guard<std::mutex> l(p_->issue);
  p_->t.begin_command(); p_->t.spi_write(c.data(), c.size()); p_->t.end_command();
}
void SramRouter::on_mbox_frame(std::function<void(const std::vector<uint8_t>&)> cb){ std::lock_guard<std::mutex> l(p_->mbox_mtx); p_->mbox_cb=std::move(cb); }
std::mutex& SramRouter::cmd_mutex_for_test(){ return p_->issue; }
}
