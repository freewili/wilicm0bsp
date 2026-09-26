#include "uart_receiver.h"
namespace fwcm0 {
void UartReceiver::start(){ running_ = true; th_ = std::thread([this]{ loop(); }); }
void UartReceiver::stop(){ if(running_.exchange(false) && th_.joinable()) th_.join(); }
void UartReceiver::loop(){
  // On the target, LinuxTransport applies SCHED_FIFO + mlockall before this runs.
  uint8_t buf[512];
  while (running_) {
    size_t n = t_.uart_read(buf, sizeof(buf), 50);   // 50 ms wake to re-check running_
    if (n) parser_.feed(buf, n, on_frame_);
  }
}
}
