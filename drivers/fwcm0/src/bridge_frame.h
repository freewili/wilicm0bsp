#pragma once
// Length-prefixed AF_UNIX frame helpers: [u32 len LE][payload]. Every request
// and unary response on the bridge socket is exactly one of these frames;
// streaming ops (CONSOLE, MONITOR) send one request frame then fall back to a
// raw, unframed byte stream. Target-only (uses <unistd.h>): kept out of
// bridge_proto.h, which IS host-built via bridge_proto.cpp/fwcm0_core, so this
// header must never be included from a host-compiled translation unit.
#include <cstddef>
#include <cstdint>
#include <unistd.h>
#include <vector>

namespace fwcm0 { namespace bridge {

// Writes exactly one [u32 len LE][payload] frame, handling partial writes.
// False on any write error (including a peer that hung up mid-frame).
inline bool write_frame(int fd, const std::vector<uint8_t>& payload) {
  uint32_t len = static_cast<uint32_t>(payload.size());
  uint8_t hdr[4] = {
    static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>((len >> 8) & 0xFF),
    static_cast<uint8_t>((len >> 16) & 0xFF), static_cast<uint8_t>((len >> 24) & 0xFF)};
  size_t off = 0;
  while (off < sizeof(hdr)) {
    ssize_t w = ::write(fd, hdr + off, sizeof(hdr) - off);
    if (w <= 0) return false;
    off += static_cast<size_t>(w);
  }
  off = 0;
  while (off < payload.size()) {
    ssize_t w = ::write(fd, payload.data() + off, payload.size() - off);
    if (w <= 0) return false;
    off += static_cast<size_t>(w);
  }
  return true;
}

// Reads exactly one [u32 len LE][payload] frame into out. False on a closed/
// errored fd, a short frame (EOF before the length is satisfied), or a
// declared length exceeding max_len (default 1 MiB) — a corrupt/hostile
// length prefix must not cause an unbounded allocation.
inline bool read_frame(int fd, std::vector<uint8_t>& out, uint32_t max_len = 1u << 20) {
  uint8_t hdr[4];
  size_t off = 0;
  while (off < sizeof(hdr)) {
    ssize_t r = ::read(fd, hdr + off, sizeof(hdr) - off);
    if (r <= 0) return false;
    off += static_cast<size_t>(r);
  }
  uint32_t len = static_cast<uint32_t>(hdr[0]) | (static_cast<uint32_t>(hdr[1]) << 8) |
                 (static_cast<uint32_t>(hdr[2]) << 16) | (static_cast<uint32_t>(hdr[3]) << 24);
  if (len > max_len) return false;
  out.assign(len, 0);
  off = 0;
  while (off < len) {
    ssize_t r = ::read(fd, out.data() + off, len - off);
    if (r <= 0) return false;
    off += static_cast<size_t>(r);
  }
  return true;
}

}} // namespace fwcm0::bridge
