#include "fwcm0/bridge_proto.h"
#include "fwcm0/transport.h"   // Status
namespace fwcm0 { namespace bridge {

std::vector<uint8_t> encode_read_req(uint32_t addr, uint16_t len){
  std::vector<uint8_t> v{OP_READ}; put_u32(v, addr); put_u16(v, len); return v;
}
bool decode_read_req(const uint8_t* p, size_t n, uint32_t& addr, uint16_t& len){
  if(!p || n < 6) { return false; }
  addr = get_u32(p);
  len = get_u16(p + 4);
  return true;
}
std::vector<uint8_t> encode_write_req(uint32_t addr, const std::vector<uint8_t>& data){
  std::vector<uint8_t> v{OP_WRITE}; put_u32(v, addr); v.insert(v.end(), data.begin(), data.end()); return v;
}
bool decode_write_req(const uint8_t* p, size_t n, uint32_t& addr, std::vector<uint8_t>& data){
  if(!p || n < 4) { return false; }
  addr = get_u32(p);
  data.assign(p + 4, p + n);
  return true;
}
std::vector<uint8_t> encode_swap_req(uint32_t timeout_ms){
  std::vector<uint8_t> v{OP_SWAP}; put_u32(v, timeout_ms); return v;
}
bool decode_swap_req(const uint8_t* p, size_t n, uint32_t& timeout_ms){
  if(!p || n < 4) { return false; }
  timeout_ms = get_u32(p);
  return true;
}

std::vector<uint8_t> encode_status_resp(const Status& s){
  std::vector<uint8_t> v{RC_OK};
  v.push_back(s.init_done); v.push_back(s.boot_ready); v.push_back(s.quiesced);
  v.push_back(s.ready); v.push_back(s.wr_active); v.push_back(s.rd_active); v.push_back(s.abort);
  v.push_back(s.last_completed_seq); put_u32(v, s.bytes_written);
  return v;
}
bool decode_status_resp(const std::vector<uint8_t>& r, Status& o){
  if(r.size() < 13 || r[0] != RC_OK) return false;
  o.init_done=r[1]; o.boot_ready=r[2]; o.quiesced=r[3]; o.ready=r[4];
  o.wr_active=r[5]; o.rd_active=r[6]; o.abort=r[7]; o.last_completed_seq=r[8];
  o.bytes_written = get_u32(&r[9]);
  return true;
}

}} // namespace fwcm0::bridge
