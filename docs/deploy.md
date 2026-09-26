# Install apps from a PC over USB

`tools/deploy.py` installs a BSP app on the CM0 from a Windows, macOS, or Linux
PC using only the USB cable to MAIN. No network, SSH, or CM0 Gadget Serial is
needed. It carries a CM0 Linux shell through MAIN's framed menu — the same path
FreeWili GUI's Linux Console uses — so it keeps working after the CM0 USB port
is switched to host mode for a peripheral.

It publishes the same self-contained folder as `fw.py install` on the device:
`/home/apps/<app>/` with `run.sh`, the app files and a bundled `.lib/`
(OneWili Python sources, CM0 adapter, `result`). Launch it on the device from
**Linux > Apps > \<app\> > run.sh**.

## One-time PC setup

```sh
git clone --recurse-submodules https://github.com/freewili/wilicm0bsp.git
cd wilicm0bsp
python -m pip install pyserial
python tools/fw.py setup
```

Turn Linux on from the PC with `python tools/deploy.py cm0 on` (FPGA and CM0
power, then waits for Linux), or from the device's power controls. MAIN
firmware must provide the framed Linux shell commands (`l\c`, `l\w`, `l\r`,
`l\e`); current FreeWili GUI Linux Console uses the same commands.

**Disconnect FreeWili GUI first** (or close the application). Only one program
can own the MAIN serial port; a busy port reports "Cannot open …".

## Commands

Run these on the PC from the BSP folder. The MAIN port is detected by USB ID;
pass `--port COM183` (Windows) or `--port /dev/ttyACM0` (Linux/macOS) before the
command when several FreeWili devices are connected.

| Command | Purpose |
| --- | --- |
| `python tools/deploy.py ports` | List FreeWili 2 MAIN serial ports |
| `python tools/deploy.py info` | CM0 model, kernel, Python, `fwcm0` and installed apps |
| `python tools/deploy.py install <app>` | Stage and copy `apps/<app>` to `/home/apps/<app>` |
| `python tools/deploy.py install <app> --replace` | Stop a running copy, back it up, then replace it |
| `python tools/deploy.py install <app> --run` | Install, start it, and print its first log lines |
| `python tools/deploy.py run <app>` | Start an installed app the way Linux > Apps does |
| `python tools/deploy.py stop <app>` | Stop a running app (SIGTERM) |
| `python tools/deploy.py log [<app>] [-n 80]` | Print the newest app log |
| `python tools/deploy.py shell "<command>"` | Run one CM0 shell command and print its output |

Example: install and try the Wi-Fi analyzer, then check its log.

```sh
python tools/deploy.py install wifi_analyzer --run
python tools/deploy.py log wifi_analyzer
```

## What install does

1. Stages `apps/<app>` on the PC with the same code as `fw.py install`. Native
   apps need an ARM64 binary in `build/apps/<app>/<app>` (built on CM0 or with a
   matching toolchain); a Windows or macOS executable cannot run on CM0.
2. Opens the CM0 shell through MAIN. If `/home/apps` is missing it tries the
   README's one-time `sudo -n install -d …`; it never changes an existing
   folder's owner. Without passwordless sudo it prints the command to run.
3. Copies a zip in base64 lines to a hidden file in `/home/apps`, verifies its
   SHA-256 on the CM0, unpacks it beside the target, marks `run.sh` (and a native
   binary) executable, and publishes with a single rename.
4. Refuses to overwrite an existing app unless `--replace` is given. With
   `--replace` it stops processes running from that folder and moves the old
   folder to `~/.local/share/fw-deploy/backups/<app>-<time>/`. App data under
   `~/.local/share/<app>/` and `~/.config/<app>/` is not touched.

Copy speed depends on MAIN firmware and the serial link; a Python app with its
bundled runtime is roughly 240 KiB. Temporary files are removed on success and
failure.

`run` and `install --run` start `run.sh` detached with `setsid`, stdin from
`/dev/null`, and output in `~/.local/state/freewili/apps/app-<app>-<time>.log`,
matching what the Linux Apps launcher does. Launching from the device's Apps
menu remains the normal way to run an app.

## CM0 power, reset and USB mode

These commands talk to MAIN's menu directly, so `status` and `usb` work even
when no CM0 shell can be opened.

| Command | Purpose |
| --- | --- |
| `python tools/deploy.py cm0 status` | Power zones, control lines and CM0 USB mode (read-only) |
| `python tools/deploy.py cm0 on` | Power FPGA (zone 6) and CM0 (zone 17, then RUN), then wait for Linux |
| `python tools/deploy.py cm0 reboot` | Clean `systemctl reboot` through the shell, then wait for Linux |
| `python tools/deploy.py cm0 reset --force` | Pulse the CM0 RUN line when the shell is unusable, then wait |
| `python tools/deploy.py cm0 power-cycle --force` | Switch CM0 power (zone 17) off and on when the shell is unusable, then wait |
| `python tools/deploy.py cm0 usb status\|host\|gadget` | Query or switch CM0 USB at runtime (`l\u`) |

MAIN exposes the relevant controls as OneWili Power Management calls: CM0 power
is **zone 17**, the CM0 RUN line is **control line 19 (`CM0_RUNPG`)**
(`h\p\c 0` holds the CPU in reset, `h\p\c 1` releases it; `h\p\n` reads the
lines back), and FPGA power is zone 6. `h\p\g` lists every zone's state.

`cm0 on` follows FreeWili GUI's Linux power sequence: switch the CM0 rail
(`h\p\s 17 1`), wait for the PIC's zone walk, then release RUN (`h\p\c 1`).
Never release RUN into an unpowered CM0, and do not set the rail and RUN with
back-to-back frames or `h\p\m` (MAIN strips RUN from the mask).

If the CM0 boots but its `fwcm0-bridge` logs `error: router timeout` and
restarts every few seconds (from the PC: sessions open but `CM0 shell input did
not drain`, and `l\u` answers `ENOREPLY`), the FPGA mailbox is not answering.
Cycling FPGA power (zone 6 off, then on) makes MAIN re-run FPGA setup and
restored the link once on the test unit; a full device reset restored it when
that did not.

Prefer `cm0 reboot`: Linux shuts down its filesystems itself. `reset` and
`power-cycle` are like pulling power — the SD card may be mid-write — so it requires `--force` and is
meant only for a CM0 that cannot be reached otherwise. Never switch zone 17 off
while Linux is running (see [hardware boundaries](../agents/hardware.md)).
MAIN only reports success for `h\p\c` after the PIC reads the pin back; on the
2026-09-26 test unit it refused the hold (`CM0_RUNPG did not read back held in
reset`), so a forced reset is not guaranteed to be available. Use FreeWili GUI's
power controls or a full device power cycle in that case.

`cm0 usb host` switches the CM0 USB port to host mode for a USB device on Port 3
(CN25) without changing the boot default. It needs a CM0 image that implements
USB-mode control; older images give no reply, and no switch is sent. Then use a
boot-configuration change instead (for example `apps/wifi_analyzer/setup_usb_host.py`,
run once with sudo, then `cm0 reboot`). Host mode disconnects CM0 Gadget Serial;
this tool and FreeWili GUI keep working because they go through MAIN.

### A stuck shell session (`EBUSY close the active Linux shell first`)

MAIN serves one CM0 bash session, shared by the on-screen **Linux Terminal**
panel, menu passthrough (`l\b`), and one framed client (`l\c`). A framed
session is released by `l\e` with its own token, or when its owner stops
reading/writing for 30 seconds. In MAIN firmware (`rpCM0Comm::shellPollOpen`),
`l\c` answers `EBUSY` when:

1. the shell is already attached — the Linux Terminal panel is open on the
   FreeWili screen, FreeWili GUI's Linux Console or passthrough is active, or
   another framed client still holds it; or
2. a previous detach is still queued for the CM0 and has not been delivered
   over the mailbox, so no new owner can attach yet.

This state lives on **MAIN**. Restarting or power-cycling only the CM0 does not
clear it. Close the Linux Terminal/Console first; if nothing else holds the
shell, power the whole FreeWili off and on to restart MAIN. On the 2026-09-26
test unit, a session whose writes stopped draining (`CM0 shell input did not
drain`) left MAIN in case 2; `cm0 power-cycle --force` did not clear it.

## Using the helpers from your own scripts

`tools/fwlink.py` is a small library with no dependencies beyond pyserial:

```python
import sys
sys.path.insert(0, "tools")
import fwlink

with fwlink.LinuxShell(fwlink.resolve_port(None)) as cm0:
    code, output = cm0.run("iw dev")          # exit code and text output
    print(code, output)
    cm0.upload(b"hello\n", "/home/pi/hello.txt")  # checksum-verified copy
```

`run()` returns `(exit_code, output)`; pass `check=True` to raise on failure.
Commands run in the CM0 console user's shell. Avoid interactive programs; a
command that waits for input times out.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `No FreeWili 2 MAIN USB port found` | USB cable is on MAIN; run `ports`; pass `--port` |
| `Cannot open …` | FreeWili GUI or another tool has the port; disconnect it |
| `MAIN did not answer` | Wrong port, or MAIN still starting; wait and retry |
| `MAIN refused the Linux shell session` / `shell did not respond` | Enable Linux/CM0 power; wait for boot; close GUI's Linux Console |
| `/home/apps … could not be created` | Run the printed `sudo install -d …` once on the CM0 |
| `already exists; use --replace` | Intended: installs never merge. Use `--replace` |
| App installed but screen blank | `python tools/deploy.py log <app>` shows the app's output and errors |
| `EBUSY close the active Linux shell first` | See [a stuck shell session](#a-stuck-shell-session-ebusy-close-the-active-linux-shell-first) |
| `CM0 shell input did not drain` | MAIN stopped accepting shell bytes. Seen once, right after a separate OneWili USB session on the same port (cause unconfirmed). Retry once; if `EBUSY` follows, restart CM0 |

A reply timeout is never replayed automatically, because the lost reply may
belong to a write that already happened. Rerun the command after checking state.
