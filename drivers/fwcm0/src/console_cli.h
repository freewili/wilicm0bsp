#pragma once
namespace fwcm0 {
// Interactive console for a TTY, or a transparent byte stream when piped.
// Prefers the running bridge daemon (AF_UNIX); falls back to owning the router
// directly when no daemon is present. Returns a process exit code.
int run_console_cli(const char* sock_path, bool api = false);
} // namespace fwcm0
