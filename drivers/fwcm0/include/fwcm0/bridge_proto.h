#pragma once
#include <cstdint>
namespace fwcm0 { namespace bridge {

// AF_UNIX socket op byte (first byte of a client request).
constexpr uint8_t OP_STATUS  = 0x01;   // unary
constexpr uint8_t OP_READ    = 0x02;   // unary: [addr u32 LE][len u16 LE]
constexpr uint8_t OP_WRITE   = 0x03;   // unary: [addr u32 LE][bytes...]
constexpr uint8_t OP_SWAP    = 0x04;   // unary: [timeout_ms u32 LE]
constexpr uint8_t OP_CONSOLE = 0x05;   // streaming duplex
constexpr uint8_t OP_MONITOR = 0x06;   // streaming: notify lines
constexpr uint8_t OP_API     = 0x07;   // streaming duplex, isolated OneWili commands

// Unary response first byte.
constexpr uint8_t RC_OK   = 0x00;
constexpr uint8_t RC_ERR  = 0x01;
constexpr uint8_t RC_BUSY = 0x02;      // e.g. a second OP_CONSOLE while one is active

constexpr const char* DEFAULT_SOCK = "/run/fwcm0-bridge.sock";

}} // namespace fwcm0::bridge

#include <cstddef>
#include <vector>
namespace fwcm0 { struct Status; }   // fwd; real definition in fwcm0/transport.h
namespace fwcm0 { namespace bridge {

inline void put_u32(std::vector<uint8_t>& v, uint32_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); }
inline void put_u16(std::vector<uint8_t>& v, uint16_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); }
inline uint32_t get_u32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
inline uint16_t get_u16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }

std::vector<uint8_t> encode_read_req(uint32_t addr, uint16_t len);
bool decode_read_req(const uint8_t* p, size_t n, uint32_t& addr, uint16_t& len);
std::vector<uint8_t> encode_write_req(uint32_t addr, const std::vector<uint8_t>& data);
bool decode_write_req(const uint8_t* p, size_t n, uint32_t& addr, std::vector<uint8_t>& data);
std::vector<uint8_t> encode_swap_req(uint32_t timeout_ms);
bool decode_swap_req(const uint8_t* p, size_t n, uint32_t& timeout_ms);

std::vector<uint8_t> encode_status_resp(const Status& s);
bool decode_status_resp(const std::vector<uint8_t>& resp, Status& out);

}} // namespace fwcm0::bridge
