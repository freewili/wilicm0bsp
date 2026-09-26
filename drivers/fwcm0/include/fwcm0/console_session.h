#pragma once
#include "fwcm0/console_client.h"
#include <cstddef>
#include <cstdint>
#include <functional>
namespace fwcm0 {

// Bridge a byte stream to MAIN's console over ConsoleClient.
// read_in(buf,n): blocking read of up to n bytes; returns count, 0 on EOF, <0 on error.
// write_out(buf,n): write n bytes to the output sink; invoked on the UART drain
//   thread via on_console, so it must be thread-safe and non-blocking.
// interactive: when true, a 0x1D byte (Ctrl-]) ends the session locally and is
//   not forwarded. eof_grace_ms: on EOF, keep delivering inbound output for this
//   long before detaching (lets pipe scripts flush MAIN's trailing output).
// Returns 0 on normal end (EOF or exit key), 1 on read error.
int run_console_session(
    ConsoleClient& client,
    const std::function<long(uint8_t* buf, size_t n)>& read_in,
    const std::function<void(const uint8_t* buf, size_t n)>& write_out,
    bool interactive,
    int eof_grace_ms = 0);

} // namespace fwcm0
