#include "wilicm0/device.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace wilicm0 {
namespace {
using Clock = std::chrono::steady_clock;
int ready(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        pollfd p{fd, events, 0};
        int rc = ::poll(&p, 1, static_cast<int>(std::clamp<long long>(left, 0, INT_MAX)));
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) return rc;
        if (p.revents & POLLNVAL) { errno = EBADF; return -1; }
        return 1; // read the final bytes even when POLLHUP accompanies POLLIN
    }
}
}

Device::Device(const char* path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!path || std::strlen(path) >= sizeof(address.sun_path))
        throw std::invalid_argument("Invalid bridge socket path");
    std::strcpy(address.sun_path, path);
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) throw std::runtime_error(std::strerror(errno));
    try {
        if (::fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0 ||
            ::fcntl(fd_, F_SETFL, O_NONBLOCK) < 0)
            throw std::runtime_error(std::strerror(errno));
#ifdef SO_NOSIGPIPE
        int enabled = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0)
            throw std::runtime_error(std::strerror(errno));
#endif
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            if (errno != EINPROGRESS || ready(fd_, POLLOUT, Clock::now() + std::chrono::seconds(3)) != 1)
                throw std::runtime_error("Cannot connect to fwcm0-bridge: " + std::string(std::strerror(errno)));
            int error = 0; socklen_t length = sizeof(error);
            if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
                throw std::runtime_error("Cannot connect to fwcm0-bridge: " + std::string(std::strerror(error ? error : errno)));
        }
        // Bridge request: uint32 little-endian payload size, then OP_API.
        const uint8_t request[] = {1, 0, 0, 0, 7};
        if (write_bytes(this, request, sizeof(request)) < 0)
            throw std::runtime_error(error_);
        ow_transport transport{this, write_bytes, read_bytes};
        if (ow_open(&device_, &transport) != OW_OK)
            throw std::runtime_error("Cannot initialize OneWili: " + error_);
        char sd[32]{}, mask[32]{}; bool hoststream = false; int32_t clock = 0;
        auto result = ow_hardware_system_device_state(&device_, sd, sizeof(sd), &hoststream, mask, sizeof(mask), &clock);
        if (result != OW_OK)
            throw std::runtime_error("OneWili connection probe failed (" + std::to_string(result) + "): " +
                (error_.empty() ? "check MAIN protocol 1.2+, bridge service, and session ownership" : error_));
        error_.clear();
    } catch (...) { close(); throw; }
}

Device::~Device() { close(); }

void Device::close() noexcept {
    if (fd_ < 0) return;
    ::shutdown(fd_, SHUT_WR);
    // EOF is the bridge's release barrier. Bound teardown if the daemon stalls.
    const auto deadline = Clock::now() + std::chrono::seconds(1);
    uint8_t discard[256];
    while (Clock::now() < deadline && ready(fd_, POLLIN, deadline) > 0) {
        auto n = ::recv(fd_, discard, sizeof(discard), 0);
        if (n <= 0 && errno != EINTR && errno != EAGAIN) break;
        if (n == 0) break;
    }
    ::close(fd_); fd_ = -1;
    // ow_close emits a serial-console reset. The bridge releases API mode on
    // EOF instead; no more writes may occur after half-closing this socket.
    device_.t = {};
}

int Device::write_bytes(void* context, const uint8_t* bytes, size_t length) {
    auto& self = *static_cast<Device*>(context);
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    size_t offset = 0;
    while (offset < length) {
        int available = ready(self.fd_, POLLOUT, deadline);
        if (available <= 0) {
            self.error_ = available == 0 ? "bridge write timed out" : std::strerror(errno);
            return -1;
        }
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        auto count = ::send(self.fd_, bytes + offset, length - offset, flags);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (Clock::now() >= deadline) { self.error_ = "bridge write timed out"; return -1; }
            continue;
        }
        if (count <= 0) { self.error_ = "bridge write failed"; return -1; }
        offset += static_cast<size_t>(count);
    }
    return static_cast<int>(offset);
}

int Device::read_bytes(void* context, uint8_t* bytes, size_t capacity, uint32_t timeout) {
    auto& self = *static_cast<Device*>(context);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        int available = ready(self.fd_, POLLIN, deadline);
        if (available <= 0) return available;
        auto count = ::recv(self.fd_, bytes, capacity, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (Clock::now() >= deadline) return 0;
            continue;
        }
        if (count <= 0) {
            if (self.error_.empty()) self.error_ = "bridge disconnected";
            return -1;
        }
        // Preserve the bridge's plain-text refusal (busy/handshake failure).
        if (count >= 6 && std::memcmp(bytes, "fwcm0:", 6) == 0)
            self.error_.assign(reinterpret_cast<char*>(bytes), std::min<size_t>(count, 256));
        return static_cast<int>(count);
    }
}
}
