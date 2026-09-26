#!/usr/bin/env python3
# Python sibling of notify_monitor.cpp: watch the FPGA's swap_ready NOTIFYs.
# Drives `fwcm0 monitor`, which streams NOTIFY lines until interrupted - stdlib
# only, no deps.
#
# Run on the CM0:  sudo python3 notify_monitor.py   (Ctrl-C to stop)
import subprocess, sys

proc = subprocess.Popen(["fwcm0", "monitor"], stdout=subprocess.PIPE, text=True)
swaps = 0
print("watching for swap_ready notifies (Ctrl-C to stop)...")
try:
    for line in proc.stdout:
        sys.stdout.write(line); sys.stdout.flush()
        if "swap" in line.lower():
            swaps += 1
except KeyboardInterrupt:
    pass
finally:
    proc.terminate()
    print(f"\n{swaps} swaps observed")
