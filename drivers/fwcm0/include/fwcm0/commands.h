#pragma once
#include <cstdint>
#include <vector>
namespace fwcm0 {
std::vector<uint8_t> cmd_write_at(uint32_t addr, const std::vector<uint8_t>& data);
std::vector<uint8_t> cmd_write_cont(const std::vector<uint8_t>& data);
std::vector<uint8_t> cmd_read_at(uint8_t seq, uint32_t addr, uint16_t len);
std::vector<uint8_t> cmd_read_cont(uint8_t seq, uint16_t len);
std::vector<uint8_t> cmd_status(uint8_t seq);
std::vector<uint8_t> cmd_swap_request();
std::vector<uint8_t> cmd_mbox(const std::vector<uint8_t>& body);
}
