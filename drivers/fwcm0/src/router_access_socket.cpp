#include "fwcm0/router_access.h"
#include "fwcm0/bridge_proto.h"
#include "bridge_frame.h"

#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fwcm0 {
namespace {
int connect_sock(const char* path) {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("bridge socket() failed");
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  std::strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) {
    ::close(fd);
    throw std::runtime_error("bridge connect() failed");
  }
  return fd;
}

// One request/response round trip: connect, write the request frame, read
// the response frame, close. Throws on any transport failure; an empty
// response is never returned successfully.
std::vector<uint8_t> roundtrip(const char* sock, const std::vector<uint8_t>& req) {
  int fd = connect_sock(sock);
  bool ok = bridge::write_frame(fd, req);
  std::vector<uint8_t> resp;
  if (ok) ok = bridge::read_frame(fd, resp);
  ::close(fd);
  if (!ok) throw std::runtime_error("bridge request failed");
  if (!resp.empty() && resp[0] != bridge::RC_OK
      && !(req[0] == bridge::OP_SWAP && resp.size() == 1)) {
    const std::string reason = resp.size() > 1
        ? std::string(resp.begin() + 1, resp.end()) : "bridge returned error";
    throw std::runtime_error(reason);
  }
  return resp;
}
} // namespace

SocketRouterAccess::SocketRouterAccess(const char* sock_path) : sock_(sock_path) {}

Status SocketRouterAccess::status() {
  std::vector<uint8_t> resp = roundtrip(sock_, {bridge::OP_STATUS});
  Status s{};
  if (!bridge::decode_status_resp(resp, s)) throw std::runtime_error("status: bad response");
  return s;
}

std::vector<uint8_t> SocketRouterAccess::read(uint32_t addr, uint16_t len) {
  std::vector<uint8_t> resp = roundtrip(sock_, bridge::encode_read_req(addr, len));
  if (resp.empty() || resp[0] != bridge::RC_OK) throw std::runtime_error("read: bridge returned error");
  return std::vector<uint8_t>(resp.begin() + 1, resp.end());
}

void SocketRouterAccess::write(uint32_t addr, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> resp = roundtrip(sock_, bridge::encode_write_req(addr, data));
  if (resp.empty() || resp[0] != bridge::RC_OK) throw std::runtime_error("write: bridge returned error");
}

bool SocketRouterAccess::swap(int timeout_ms) {
  std::vector<uint8_t> resp = roundtrip(sock_, bridge::encode_swap_req(static_cast<uint32_t>(timeout_ms)));
  return !resp.empty() && resp[0] == bridge::RC_OK;
}

void SocketRouterAccess::monitor(const std::function<void()>& on_swap_ready, const std::atomic<bool>& run) {
  int fd = connect_sock(sock_);
  if (!bridge::write_frame(fd, {bridge::OP_MONITOR})) {
    ::close(fd);
    throw std::runtime_error("monitor: bridge request failed");
  }
  // Streaming: the daemon pushes an unframed "swap_ready\n" line per notify.
  // Poll with a short timeout so `run` (flipped by the CLI's SIGINT handler)
  // is rechecked promptly instead of blocking forever in ::read().
  std::string buf;
  char tmp[256];
  while (run) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = ::poll(&pfd, 1, 200);
    if (pr < 0) break;
    if (pr == 0) continue;
    if (pfd.revents & (POLLHUP | POLLERR)) break;
    ssize_t n = ::read(fd, tmp, sizeof(tmp));
    if (n <= 0) break;
    buf.append(tmp, static_cast<size_t>(n));
    size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      buf.erase(0, pos + 1);
      on_swap_ready();
    }
  }
  ::close(fd);
}

} // namespace fwcm0
