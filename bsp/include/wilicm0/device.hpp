#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "onewili.h"

namespace wilicm0 {
// An isolated OneWili API session through the running fwcm0 bridge.
// One session per CM0; serialize access from your application's threads.
// No direct GPIO/SPI ownership and no service stop are needed.
class Device {
public:
    explicit Device(const char* socket_path = "/run/fwcm0-bridge.sock");
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ow_device* get() noexcept { return &device_; }
    const std::string& error() const noexcept { return error_; }
private:
    static int write_bytes(void*, const uint8_t*, size_t);
    static int read_bytes(void*, uint8_t*, size_t, uint32_t);
    void close() noexcept;
    int fd_ = -1;
    ow_device device_{};
    std::string error_;
};
}
