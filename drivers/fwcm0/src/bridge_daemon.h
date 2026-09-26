#pragma once
#include "fwcm0/sram_router.h"
namespace fwcm0 {
// Own the router as the single mailbox owner: serve a bash PTY over TYPE_SHELL and
// accept local AF_UNIX clients (console relay + register ops). Blocks until a fatal
// error or SIGINT/SIGTERM. Returns 0 on clean shutdown, 1 on setup failure.
int run_bridge_daemon(SramRouter& router, const char* sock_path);
} // namespace fwcm0
