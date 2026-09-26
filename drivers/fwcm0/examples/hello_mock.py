#!/usr/bin/env python3
# Python sibling of hello_mock.cpp. The C++ example drives the SramRouter API
# against a MockTransport; there is no Python binding for the C++ library, so
# the Python examples drive the `fwcm0` CLI instead - stdlib only, no deps.
# This is the "hello": a single STATUS round-trip to the FPGA router.
#
# Run on the CM0:  sudo python3 hello_mock.py
import subprocess, sys

r = subprocess.run(["fwcm0", "status"], capture_output=True, text=True)
sys.stdout.write(r.stdout)
if r.returncode != 0:
    sys.stderr.write(r.stderr)
    sys.exit("status failed (FPGA router attached? running as root?)")

# "init_done=1 boot_ready=1 quiesced=0 ..." -> dict
f = dict(tok.split("=", 1) for tok in r.stdout.split() if "=" in tok)
print(f"open ok: init_done={f.get('init_done')} boot_ready={f.get('boot_ready')}")
