#include "bridge_daemon.h"
#include "bridge_frame.h"
#include "fwcm0/bridge_proto.h"
#include "fwcm0/mbox_mux.h"
#include "fwcm0/console_client.h"
#include "fwcm0/console_session.h"
#include "fwcm0/shell_server.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <grp.h>
#include <poll.h>
#include <pty.h>          // forkpty (link -lutil)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

namespace fwcm0 {
using bridge::OP_CONSOLE; using bridge::OP_STATUS; using bridge::OP_READ;
using bridge::OP_WRITE; using bridge::OP_SWAP; using bridge::OP_MONITOR;
using bridge::RC_OK; using bridge::RC_ERR; using bridge::RC_BUSY;

namespace {
std::atomic<bool> g_run{true};
// Write end of the accept loop's self-pipe. Poking it is what actually gets us
// out of poll(); clearing g_run alone loses a signal that lands between the
// loop test and the poll.
int g_wake_fd = -1;
void on_signal(int) {
  g_run = false;
  if (g_wake_fd >= 0) { char c = 0; ssize_t w = ::write(g_wake_fd, &c, 1); (void)w; }
}

// A running bash PTY session. Spawned on SHELL_ATTACH[1], torn down on [0].
class BashPty {
public:
  explicit BashPty(ShellServer& shell) : shell_(shell) {}
  ~BashPty() { stop(); }

  void start() {
    std::unique_lock<std::mutex> lk(m_);
    if (pid_ > 0) {
      // Repeat attach: reap a bash that already self-exited (non-blocking)
      // so we fall through and spawn a fresh one instead of returning as
      // if the old one were still alive. MAIN ref-counts attach so this is
      // not normally reached, but start() should be robust regardless.
      int st = 0;
      // exited_ covers a child already reaped elsewhere, which waitpid can then
      // no longer report: without it a self-exited session leaves pid_ set and
      // every later attach returns early below as though bash were still alive.
      if (exited_ || ::waitpid(pid_, &st, WNOHANG) == pid_) {
        stopping_ = true;                      // the close below is deliberate, not a self-exit
        // Mirror stop()'s capture-release-join shape: read_loop() re-acquires
        // m_ every iteration, so joining it while still holding m_ here would
        // deadlock if it's parked draining buffered PTY output. Capture,
        // clear the members, release the lock, THEN close+join outside it.
        int old_master = master_;
        std::thread old_reader = std::move(reader_);
        master_ = -1;
        pid_ = -1;
        lk.unlock();
        if (old_master >= 0) ::close(old_master);      // unblocks read_loop if still parked
        if (old_reader.joinable()) old_reader.join();
        lk.lock();
      }
    }
    if (pid_ > 0) return;                      // idempotent (repeat attach, still alive)
    stopping_ = false;                         // arm the self-exit notice for the new session
    exited_ = false;
    winsize ws{}; ws.ws_row = 24; ws.ws_col = 80;
    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) { std::perror("forkpty"); return; }
    if (pid == 0) {                            // child: real login as pi
      // setenv() is not async-signal-safe post-fork in a multithreaded
      // process; pass the environment straight to exec instead.
      char* env[] = { (char*)"TERM=xterm", nullptr };
      execle("/bin/login", "login", "-f", "pi", (char*)nullptr, env);
      _exit(127);
    }
    pid_ = pid; master_ = master;
    reader_ = std::thread([this]{ read_loop(); });
  }

  void stop() {
    stopping_ = true;                          // suppress the self-exit notice below
    pid_t pid; int master;
    { std::lock_guard<std::mutex> l(m_); pid = pid_; master = master_; pid_ = -1; master_ = -1; }
    if (pid <= 0) return;
    ::kill(pid, SIGHUP);
    if (master >= 0) ::close(master);          // unblocks read_loop
    if (reader_.joinable()) reader_.join();
    int st = 0; ::waitpid(pid, &st, 0);        // reap
  }

  void write_in(const uint8_t* d, size_t n) {
    int master; { std::lock_guard<std::mutex> l(m_); master = master_; }
    if (master < 0) return;
    size_t off = 0;
    while (off < n) { ssize_t w = ::write(master, d + off, n - off); if (w <= 0) break; off += (size_t)w; }
  }

private:
  void read_loop() {
    uint8_t buf[256];
    int eio_retries = 0;                        // bounds a PTY that errors forever
    for (;;) {
      int master; { std::lock_guard<std::mutex> l(m_); master = master_; }
      if (master < 0) break;
      ssize_t r = ::read(master, buf, sizeof(buf));
      // SIGCHLD from reaping a previous session interrupts this read.
      if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      if (r < 0 && !stopping_) {
        // A PTY master also reports EIO transiently around setup/teardown, so
        // the session is only over once the child is actually gone.
        pid_t pid; { std::lock_guard<std::mutex> l(m_); pid = pid_; }
        int st = 0;
        if (pid > 0 && ::waitpid(pid, &st, WNOHANG) == 0 && ++eio_retries < 200) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          continue;                             // ~1s, then report it ended
        }
      }
      if (r <= 0) {                             // bash exited or master closed
        // Only a self-exit is news to MAIN; a deliberate close sets stopping_
        // first, and reporting it would tear down the next attach.
        if (!stopping_) { exited_ = true; shell_.send_shell_exit(); }
        break;
      }
      eio_retries = 0;
      shell_.send_shell(buf, (size_t)r);        // paced by the shared mux
    }
  }
  ShellServer& shell_;
  std::mutex m_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> exited_{false};
  pid_t pid_ = -1; int master_ = -1;
  std::thread reader_;
};

// One console client at a time (atomic guard around the streaming session).
std::atomic<bool> g_console_busy{false};

// Registry of live client fds so shutdown can wake parked ::read()s without
// an fd-reuse race: a fd is inserted in the accept loop right after accept()
// succeeds (before the client thread is even spawned, so a thread that
// hasn't started running yet still gets shutdown()'d) and erased (under
// fds_mtx) BEFORE it is closed on every exit path, so shutdown never
// shutdown()s a closed/reused fd.
void erase_and_close(std::mutex& fds_mtx, std::set<int>& live_fds, int fd) {
  { std::lock_guard<std::mutex> l(fds_mtx); live_fds.erase(fd); }
  ::close(fd);
}

void serve_console(int cfd, ConsoleClient& console, std::mutex& fds_mtx, std::set<int>& live_fds, bool api) {
  if (g_console_busy.exchange(true)) {          // refuse a 2nd console
    // OP_CONSOLE switches the socket to a raw byte stream (see bridge_frame.h),
    // so the client is past frame parsing: a refusal has to be plain text or it
    // lands on the user's terminal as binary.
    static const char busy[] = "fwcm0: console busy (another session is active)\r\n";
    ssize_t w = ::write(cfd, busy, sizeof(busy) - 1); (void)w;
    erase_and_close(fds_mtx, live_fds, cfd); return;
  }
  struct Guard { ~Guard(){ g_console_busy = false; } } guard;

  bool connected = false;
  try { connected = console.connect(1000, api); }
  catch (const std::exception& e) {
    const std::string error = std::string("fwcm0: ") + e.what() + "\r\n";
    ssize_t w = ::write(cfd, error.data(), error.size()); (void)w;
    erase_and_close(fds_mtx, live_fds, cfd); return;
  }
  if (!connected) {                           // MAIN not answering
    static const char nohello[] = "fwcm0: MAIN did not complete mailbox handshake\r\n";
    ssize_t w = ::write(cfd, nohello, sizeof(nohello) - 1); (void)w;
    erase_and_close(fds_mtx, live_fds, cfd); return;
  }
  auto read_in = [cfd](uint8_t* b, size_t n) -> long { return (long)::read(cfd, b, n); };
  std::mutex om;
  // Called on the router RX thread with mbox_mtx held, so it must not block:
  // a peer that stops draining for 200 ms is dropped rather than left holding
  // that lock and stalling every other bridge client behind it.
  auto write_out = [cfd, &om](const uint8_t* b, size_t n) {
    std::lock_guard<std::mutex> l(om);
    size_t off = 0;
    while (off < n) {
      pollfd p{}; p.fd = cfd; p.events = POLLOUT;
      int pr = ::poll(&p, 1, 200);
      if (pr < 0 && errno == EINTR) continue;
      if (pr <= 0) { ::shutdown(cfd, SHUT_RDWR); return; }
      ssize_t w = ::write(cfd, b + off, n - off);
      if (w <= 0) break;
      off += (size_t)w;
    }
  };
  run_console_session(console, read_in, write_out, /*interactive=*/false, /*grace=*/0);
  console.set_stream(false);
  erase_and_close(fds_mtx, live_fds, cfd);
}

// Streams "swap_ready\n" (unframed, one line per notify) until the client
// closes or the daemon is shutting down. SramRouter::on_swap_ready has one
// callback slot process-wide, so a second concurrent monitor client would
// silently steal it from the first; acceptable for a bench tool, not fanned
// out to multiple listeners.
void serve_monitor(int cfd, SramRouter& router, std::mutex& fds_mtx, std::set<int>& live_fds) {
  // The callback runs on the router RX thread with swap_mtx held, so it must
  // not block: it only pokes this pipe (non-blocking, one byte per notify) and
  // this thread does the socket write.
  int nfy[2] = {-1, -1};
  if (::pipe2(nfy, O_CLOEXEC | O_NONBLOCK) < 0) { erase_and_close(fds_mtx, live_fds, cfd); return; }
  int wfd = nfy[1];
  router.on_swap_ready([wfd]{ char c = 0; ssize_t w = ::write(wfd, &c, 1); (void)w; });
  static const char msg[] = "swap_ready\n";
  uint8_t tmp[64];
  while (g_run) {
    pollfd p[2] = {{cfd, POLLIN, 0}, {nfy[0], POLLIN, 0}};
    if (::poll(p, 2, -1) < 0) { if (errno == EINTR) continue; break; }
    if (p[0].revents) {
      ssize_t r = ::read(cfd, tmp, 1);            // unblocked by shutdown() at teardown
      if (r <= 0) break;                          // client closed (or a stray byte we ignore otherwise)
    }
    if (p[1].revents & POLLIN) {
      ssize_t got = ::read(nfy[0], tmp, sizeof(tmp));
      bool broken = false;
      for (ssize_t i = 0; i < got && !broken; ++i) {
        size_t off = 0, n = sizeof(msg) - 1;
        while (off < n) { ssize_t w = ::write(cfd, msg + off, n - off); if (w <= 0) { broken = true; break; } off += (size_t)w; }
      }
      if (broken) break;
    }
  }
  router.on_swap_ready(nullptr);                  // unregister before the pipe/cfd are closed below
  ::close(nfy[0]); ::close(nfy[1]);
  erase_and_close(fds_mtx, live_fds, cfd);
}

void handle_client(int cfd, ConsoleClient& console, SramRouter& router, std::mutex& fds_mtx, std::set<int>& live_fds) {
  // cfd is already registered in live_fds by the accept loop before this
  // thread was spawned; only erase it (before close) on the way out.
  std::vector<uint8_t> req;
  if (!bridge::read_frame(cfd, req) || req.empty()) { erase_and_close(fds_mtx, live_fds, cfd); return; }
  uint8_t op = req[0];
  if (op == OP_CONSOLE || op == bridge::OP_API) { serve_console(cfd, console, fds_mtx, live_fds, op == bridge::OP_API); return; }
  if (op == OP_MONITOR) { serve_monitor(cfd, router, fds_mtx, live_fds); return; }

  const uint8_t* args = req.data() + 1;
  size_t nargs = req.size() - 1;
  std::vector<uint8_t> resp;
  try {
    if (op == OP_STATUS) {
      resp = bridge::encode_status_resp(router.status());
    } else if (op == OP_READ) {
      uint32_t addr; uint16_t len;
      if (!bridge::decode_read_req(args, nargs, addr, len)) {
        resp = {RC_ERR};
      } else {
        std::vector<uint8_t> data = router.read(addr, len);
        resp.reserve(1 + data.size());
        resp.push_back(RC_OK);
        resp.insert(resp.end(), data.begin(), data.end());
      }
    } else if (op == OP_WRITE) {
      uint32_t addr; std::vector<uint8_t> data;
      if (!bridge::decode_write_req(args, nargs, addr, data)) {
        resp = {RC_ERR};
      } else {
        router.write(addr, data);
        resp = {RC_OK};
      }
    } else if (op == OP_SWAP) {
      uint32_t timeout_ms;
      if (!bridge::decode_swap_req(args, nargs, timeout_ms)) {
        resp = {RC_ERR};
      } else {
        uint8_t rc = router.request_swap_and_wait(static_cast<int>(timeout_ms)) ? RC_OK : RC_ERR;
        resp.assign(1, rc);
      }
    } else {
      resp = {RC_ERR};   // unknown op
    }
  } catch (const std::exception& e) {
    // Keep the existing status byte for older clients; newer clients can
    // explain a router timeout instead of reporting a malformed response.
    const std::string reason = e.what();
    resp.assign(1, RC_ERR);
    resp.insert(resp.end(), reason.begin(), reason.end());
    std::fprintf(stderr, "bridge request 0x%02x: %s\n", op, e.what());
  } catch (...) {
    resp = {RC_ERR};
  }
  bridge::write_frame(cfd, resp);
  erase_and_close(fds_mtx, live_fds, cfd);
}

int make_listener(const char* path) {
  ::unlink(path);
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) { std::perror("socket"); return -1; }
  sockaddr_un a{}; a.sun_family = AF_UNIX; std::strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
  if (::bind(fd, (sockaddr*)&a, sizeof(a)) < 0) { std::perror("bind"); ::close(fd); return -1; }
  // Group must be set here, not by the unit: an ExecStartPost chgrp races this
  // bind (the daemon polls the FPGA for boot_ready first), leaving the socket
  // root:root and every non-root client falling back to the busy transport.
  if (group* g = ::getgrnam("dialout"))
    (void)::chown(path, (uid_t)-1, g->gr_gid);
  else
    std::fprintf(stderr, "bridge: no dialout group; socket group unchanged\n");
  ::chmod(path, 0660);
  if (::listen(fd, 4) < 0) { std::perror("listen"); ::close(fd); return -1; }
  return fd;
}
} // namespace

int run_bridge_daemon(SramRouter& router, const char* sock_path) {
  int wake[2] = {-1, -1};                        // armed before the handlers so no signal is lost
  if (::pipe2(wake, O_CLOEXEC) < 0) { std::perror("pipe2"); return 1; }
  g_wake_fd = wake[1];

  // sigaction with sa_flags 0, not std::signal: glibc's signal() implies
  // SA_RESTART, which would silently resume the accept poll below and leave
  // systemctl stop waiting out TimeoutStopSec for a SIGKILL.
  struct sigaction sa{};
  sa.sa_handler = on_signal;
  ::sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGTERM, &sa, nullptr);
  std::signal(SIGPIPE, SIG_IGN);                 // a departed socket client must not kill us

  MboxMux mux(router);
  ConsoleClient console(mux);
  ShellServer shell(mux);
  BashPty bash(shell);

  shell.on_attach([&](bool en){ if (en) bash.start(); else bash.stop(); });
  shell.on_shell_data([&](const uint8_t* d, size_t n){ bash.write_in(d, n); });

  int lfd = make_listener(sock_path);
  if (lfd < 0) { g_wake_fd = -1; ::close(wake[0]); ::close(wake[1]); return 1; }

  // Tracked (not detached) client threads + their live fds, so shutdown can
  // deterministically wake and join every in-flight client before bash/shell/
  // console/mux destruct below. See Finding 1: an fd is never shutdown()'d
  // after it is closed/reused because handle_client/serve_console erase it
  // from live_fds (under fds_mtx) before every ::close. unique_ptr keeps a
  // Client's address stable as the vector grows, so its thread can flag itself.
  struct Client { std::thread th; std::atomic<bool> done{false}; };
  std::mutex clients_mtx;
  std::vector<std::unique_ptr<Client>> clients;
  std::mutex fds_mtx;
  std::set<int> live_fds;

  // Accept loop. The router's internal command mutex serializes client access;
  // g_console_busy caps console to one. Finished threads are joined and dropped
  // on each accept, so the vector stays bounded by the live client count: one
  // CLI call is one thread, and a polling caller would otherwise exhaust the
  // process's thread stacks.
  while (g_run) {
    pollfd p[2] = {{lfd, POLLIN, 0}, {wake[0], POLLIN, 0}};
    if (::poll(p, 2, -1) < 0) { if (errno == EINTR) continue; break; }
    if (!g_run || (p[1].revents & POLLIN)) break;
    if (!(p[0].revents & POLLIN)) continue;
    int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) { if (!g_run) break; continue; }
    { std::lock_guard<std::mutex> l(fds_mtx); live_fds.insert(cfd); }
    std::lock_guard<std::mutex> l(clients_mtx);
    for (size_t i = 0; i < clients.size();) {
      if (clients[i]->done) { clients[i]->th.join(); clients.erase(clients.begin() + i); }
      else ++i;
    }
    clients.emplace_back(std::make_unique<Client>());
    Client* c = clients.back().get();
    c->th = std::thread([c, cfd, &console, &router, &fds_mtx, &live_fds]{
      try {
        handle_client(cfd, console, router, fds_mtx, live_fds);
      } catch (const std::exception& e) {
        // A failed hardware write during HELLO must not terminate the daemon
        // (uncaught exceptions in a client thread otherwise call terminate).
        std::fprintf(stderr, "bridge client: %s\n", e.what());
        erase_and_close(fds_mtx, live_fds, cfd);
      }
      c->done = true;                            // last statement: the join above must not wait
    });
  }

  // Shutdown, in order: (1) shutdown() every live client fd FIRST. A console
  // write_out can be blocked in ::write(cfd,...) under mbox_mtx (held by the
  // RX thread across the dispatch call) if the peer stopped draining the
  // socket; if the barrier below ran first it would block on that same
  // mbox_mtx forever, with the shutdown() that would unblock the write
  // (making it return EPIPE) never reached. Doing the shutdown loop first
  // means that write is already unblocked before we ever wait on mbox_mtx.
  // (2) quiesce RX dispatch: SramRouter::on_mbox_frame takes the same
  // mbox_mtx the RX thread holds across the handler call, so this blocks
  // until any in-flight shell/console handler (now unblocked by step 1)
  // returns; once it returns, no more dispatch can reach bash/shell/console.
  // (The MboxMux dtor calls this again later; that's idempotent.) (3) join
  // every client thread, so none is left mid handle_client/run_console_session
  // using console/mux. Only then do bash/shell/console/mux (locals below)
  // destruct.
  {
    std::lock_guard<std::mutex> l(fds_mtx);
    for (int fd : live_fds) ::shutdown(fd, SHUT_RDWR);
  }
  router.on_mbox_frame(nullptr);
  {
    std::lock_guard<std::mutex> l(clients_mtx);
    for (auto& c : clients) if (c->th.joinable()) c->th.join();
  }

  g_wake_fd = -1;
  ::close(wake[0]); ::close(wake[1]);
  ::close(lfd); ::unlink(sock_path);
  return 0;
}

} // namespace fwcm0
