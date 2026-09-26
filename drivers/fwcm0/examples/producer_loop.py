#!/usr/bin/env python3
# Python sibling of producer_loop.cpp: fill a buffer, write it to FPGA SRAM,
# request a swap, and block until the swap completes before reusing the buffer.
# Drives the `fwcm0` CLI (write + swap) - stdlib only, no deps.
#
# Run on the CM0:  sudo python3 producer_loop.py
import subprocess, sys, os, tempfile

BUF_ADDR = 0x000000
BUF_LEN  = 4096

def fwcm0(*args):
    return subprocess.run(["fwcm0", *args]).returncode

for frame in range(16):
    # Produce a frame of data (a rolling pattern, like the C++ example).
    buf = bytes((frame + i) & 0xFF for i in range(BUF_LEN))
    # The CLI takes hex on the cmdline or @file for raw bytes; use a temp file
    # so a 4 KB buffer doesn't blow the argv limit.
    fd, path = tempfile.mkstemp()
    try:
        os.write(fd, buf); os.close(fd)
        if fwcm0("write", f"{BUF_ADDR:x}", f"@{path}") != 0:
            sys.exit(f"frame {frame}: write failed")
    finally:
        os.unlink(path)
    # request_swap + block on the swap_ready NOTIFY (10 s limit), so the next
    # iteration never overwrites the buffer the consumer is still reading.
    if fwcm0("swap") != 0:
        sys.exit(f"frame {frame}: swap timed out")
    print(f"frame {frame}: {BUF_LEN} bytes produced and swapped")
