#include "fwcm0/commands.h"
#include "fwcm0/protocol.h"
#include <stdexcept>
namespace fwcm0 {
static void push_addr(std::vector<uint8_t>& v, uint32_t a){ v.push_back(static_cast<uint8_t>((a>>16)&0xFF)); v.push_back(static_cast<uint8_t>((a>>8)&0xFF)); v.push_back(static_cast<uint8_t>(a&0xFF)); }
static void push_len(std::vector<uint8_t>& v, uint16_t l){ v.push_back(static_cast<uint8_t>((l>>8)&0xFF)); v.push_back(static_cast<uint8_t>(l&0xFF)); }
std::vector<uint8_t> cmd_write_at(uint32_t addr, const std::vector<uint8_t>& data){ std::vector<uint8_t> v{OP_WRITE_AT}; push_addr(v,addr); v.insert(v.end(),data.begin(),data.end()); return v; }
std::vector<uint8_t> cmd_write_cont(const std::vector<uint8_t>& data){ std::vector<uint8_t> v{OP_WRITE_CONT}; v.insert(v.end(),data.begin(),data.end()); return v; }
std::vector<uint8_t> cmd_read_at(uint8_t seq, uint32_t addr, uint16_t len){ std::vector<uint8_t> v{OP_READ_AT, seq}; push_addr(v,addr); push_len(v,len); return v; }
std::vector<uint8_t> cmd_read_cont(uint8_t seq, uint16_t len){ std::vector<uint8_t> v{OP_READ_CONT, seq}; push_len(v,len); return v; }
std::vector<uint8_t> cmd_status(uint8_t seq){ return {OP_STATUS, seq}; }
std::vector<uint8_t> cmd_swap_request(){ return {OP_SWAP_REQUEST}; }
std::vector<uint8_t> cmd_mbox(const std::vector<uint8_t>& body){
  // Bodies over 32 bytes would overflow the router inbox.
  if(body.empty() || body.size()>32) throw std::invalid_argument("mbox body must be 1..32 bytes");
  std::vector<uint8_t> v{OP_MBOX, static_cast<uint8_t>(body.size())};
  v.insert(v.end(), body.begin(), body.end()); return v;
}
}
