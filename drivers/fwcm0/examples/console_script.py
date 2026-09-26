#!/usr/bin/env python3
# Drive MAIN's fwMenuMain from the CM0 over the console bridge, for autonomous
# control: read a value MAIN prints, decide, then send a command back. This is
# the pattern for "read accelerometer -> trigger lightshow" / "monitor radio ->
# transmit on a trigger" style automation.
#
# Run on the CM0 (Pi):  sudo python3 console_script.py
# Pure stdlib (no pexpect): drives `fwcm0 console` in its piped byte-stream mode
# via subprocess, with a reader thread + an expect() that waits for a marker.
#
# `fwcm0 console` opens ONE persistent bridge session. On connect, MAIN streams
# its menu (banner, IO state, SD card, file system). We wait for a marker, send
# a key, capture the response, act, then exit with Ctrl-] (0x1D).
#
# NOTE: the markers/keys below match the stock fwMenuMain text menu. Adapt them
# to whatever command/readout your automation needs. Bulk payloads (radio
# samples, image frames) do NOT go through this console; they ride the
# ping-pong PSRAM via `fwcm0 write/read/swap`; the console issues the command
# that tells MAIN to act on the buffered data.
import subprocess, sys, time, threading, os

proc = subprocess.Popen(["fwcm0", "console"],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)

_buf = bytearray()
_lock = threading.Lock()

def _reader():
    fd = proc.stdout.fileno()
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        with _lock:
            _buf.extend(chunk)
        sys.stdout.buffer.write(chunk); sys.stdout.flush()   # mirror MAIN's output
threading.Thread(target=_reader, daemon=True).start()

def send(s):
    proc.stdin.write(s.encode() if isinstance(s, str) else s); proc.stdin.flush()

def expect(marker, timeout=5):
    end = time.time() + timeout
    needle = marker.encode()
    while time.time() < end:
        with _lock:
            if needle in _buf:
                return True
        time.sleep(0.05)
    return False

try:
    # MAIN renders fwMenuMain on a redraw, not merely on connect. Nudge it with
    # Ctrl-C (reset/redraw) then Enter, like console_test, so the menu streams.
    send("\x03")                              # Ctrl-C: reset/redraw the menu
    send("\r")                                # Enter: trigger a render
    if not expect("---- IO state", timeout=5):
        sys.exit("menu did not arrive (is MAIN attached?)")
    time.sleep(0.3)                           # let the rest of the panel land
    with _lock:
        panel = _buf.decode("utf-8", "replace")

    # Example decision: only proceed if MAIN owns the SD card.
    if "SD Card Host: Main" in panel:
        print("\n[script] MAIN holds the SD card; proceeding.")
        with _lock:
            _buf.clear()
        send("i")                             # 'i' redraws the IO/menu state
        expect("---- IO state", timeout=5)
        # ... parse _buf here, then trigger the next action, e.g.:
        #   send("g")   # hand off to the graphical/display path
    else:
        print("\n[script] MAIN does not hold the SD card; skipping.")

finally:
    send("\x1d")                              # Ctrl-] : exit the console cleanly
    proc.stdin.close()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.terminate()
