#!/usr/bin/env python3
# Python sibling of console_test.cpp: bring up the CM0<->MAIN console mailbox
# bridge, nudge MAIN's fwMenuMain, and print whatever console text MAIN streams
# back. Drives `fwcm0 console` in its piped (non-interactive) byte-stream mode,
# stdlib only, no pexpect.
#
# Run on the CM0:  sudo python3 console_test.py
import subprocess, sys, time, threading, os

proc = subprocess.Popen(["fwcm0", "console"],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)

rx = 0
def pump():
    global rx
    fd = proc.stdout.fileno()
    while True:
        chunk = os.read(fd, 4096)       # returns whatever is available, or b"" at EOF
        if not chunk:
            break
        sys.stdout.buffer.write(chunk); sys.stdout.flush()
        rx += len(chunk)
threading.Thread(target=pump, daemon=True).start()

print("----- MAIN MENU OUTPUT -----")
# MAIN renders fwMenuMain on a redraw, not merely on connect: Ctrl-C then Enter.
proc.stdin.write(b"\x03"); proc.stdin.flush()   # Ctrl-C: reset/redraw
time.sleep(0.3)
proc.stdin.write(b"\r");  proc.stdin.flush()    # Enter: trigger a render
time.sleep(6)                                    # collect ~6 s of output

proc.stdin.write(b"\x1d"); proc.stdin.flush()   # Ctrl-] : exit the console
proc.stdin.close()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.terminate()
print(f"\n----- END ({rx} console bytes received) -----")
