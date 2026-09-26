#include "fwcm0/shell_server.h"
#include "fwcm0/protocol.h"
namespace fwcm0 {

ShellServer::ShellServer(MboxMux& mux) : mux_(mux) {
  mux_.on_type(MBOX_TYPE_SHELL, [this](const std::vector<uint8_t>& body){
    std::function<void(const uint8_t*, size_t)> cb;
    { std::lock_guard<std::mutex> l(cb_mtx_); cb = data_cb_; }
    if (cb && body.size() > 1) cb(body.data() + 1, body.size() - 1);
  });
  mux_.on_control(MBOX_CTRL_SHELL_ATTACH, [this](const std::vector<uint8_t>& body){
    std::function<void(bool)> cb;
    { std::lock_guard<std::mutex> l(cb_mtx_); cb = attach_cb_; }
    if (cb && body.size() >= 3) cb(body[2] != 0);
  });
}

ShellServer::~ShellServer() {
  mux_.on_type(MBOX_TYPE_SHELL, nullptr);
  mux_.on_control(MBOX_CTRL_SHELL_ATTACH, nullptr);
}

void ShellServer::on_attach(std::function<void(bool)> cb){
  std::lock_guard<std::mutex> l(cb_mtx_); attach_cb_ = std::move(cb);
}
void ShellServer::on_shell_data(std::function<void(const uint8_t*, size_t)> cb){
  std::lock_guard<std::mutex> l(cb_mtx_); data_cb_ = std::move(cb);
}
void ShellServer::send_shell(const uint8_t* data, size_t n){
  if (n) mux_.send_chunked(MBOX_TYPE_SHELL, data, n);
}
void ShellServer::send_shell_exit(){
  mux_.send(std::vector<uint8_t>{ MBOX_TYPE_CONTROL, MBOX_CTRL_SHELL_EXIT });
}

} // namespace fwcm0
