#pragma once
#include "fwcm0/transport.h"
#include "fwcm0/frame_parser.h"
#include <thread>
#include <atomic>
#include <functional>
namespace fwcm0 {
class UartReceiver {
public:
  UartReceiver(Transport& t, std::function<void(const Frame&)> on_frame)
    : t_(t), on_frame_(std::move(on_frame)) {}
  ~UartReceiver(){ stop(); }
  void start();
  void stop();
private:
  void loop();
  Transport& t_; std::function<void(const Frame&)> on_frame_;
  FrameParser parser_; std::thread th_; std::atomic<bool> running_{false};
};
}
