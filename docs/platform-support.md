# Platform and verification status

The product runtime is CM0 Linux (current image: ARM64 Debian 13 / Trixie).
Windows, macOS and Linux are supported development hosts for editing, Python
setup/staging, and portable driver tests. Build native ARM64 applications on
CM0 or with a matching cross toolchain/sysroot; a desktop build is not a CM0
executable. Linux-only hardware adapters are intentionally not compiled into
Windows or macOS applications.

Initial verification, 2026-09-26:

| Check | Result |
| --- | --- |
| Windows MinGW driver core and application installer | Passed CTest |
| OneWili Python, C and Rust regression suites | [Passed Windows, macOS and Linux CI](https://github.com/freewili/onewili/actions/runs/36252998900): 1,195 Python tests, C binary tests, 9 Rust tests |
| BSP Windows/MSVC, macOS and Linux host builds/tests | [Passed](https://github.com/freewili/wilicm0bsp/actions): 59 driver cases, 4 application-tool cases; Linux/macOS also run 3 socket cases |
| Linux libgpiod 2.x driver build and staged install | Passed on Debian Trixie in CI |
| CM0 ARM64 native build | Passed: core, Linux hardware driver/CLI, C++ adapter and example |
| CM0 native CTest | Passed all 3 targets: driver core, app tools and socket adapter |
| Real mailbox calls | Python Device State, ten GPIO reads, and C++ Device State passed |
| Reconnect and deployed apps | Ten consecutive native app sessions passed; installed Python/C++ launchers run from outside the checkout |
| Linux Apps launch | Production launcher agent listed the installed apps, launched `run.sh`, and produced the expected app log |
| Downloadable Python app | Extracted release payload ran on CM0 with bundled dependencies |

Hardware environment: FreeWili 2 CM0, ARM64 Debian 13 Trixie, GCC 14,
Python 3.13.5, libgpiod 2.2.1, glibc 2.41. The installed older bridge lacked
`fwcm0 api`; rebuilding and installing this BSP's CLI/bridge resolved that
mismatch. MAIN firmware was not replaced during this verification.
`doctor` now detects the older CLI explicitly.

The launcher agent was exercised directly on the device; physical screen/menu
navigation was not re-tested. Windows/macOS CI validates development tools
and portable protocol code, not direct access to FreeWili hardware on those
hosts. Cross-compilation toolchains were not tested; native CM0 compilation was.
The native release example targets ARM64 Trixie; older glibc/image combinations
are unverified. The device clock was behind source timestamps, so release
archives normalize file timestamps to avoid clock-skew build warnings.

FTDI binary event forwarding and USB directory-list events are not implemented
on CM0. Firmware peripheral behavior beyond the examples is not exhaustively
tested.

## PC deployment tools and Wi-Fi analyzer example

Added on the `feature/wifi-analyzer-and-pc-deploy` branch:

| Check | Result |
| --- | --- |
| `tools/deploy.py` / `tools/fwlink.py` against a simulated MAIN framed shell | Passed on Windows (Python 3.12): reply parsing, 192-byte chunking, exit codes, checksum-verified upload, install, overwrite refusal, `--replace` backup, cleanup |
| Staging `wifi_analyzer` on Windows | Passed: 238 KiB zip with bundled runtime |
| `wifi_analyzer` parsing/analysis (`iw` and `nmcli` formats) | Passed 12 unit tests on Windows |
| Deploy tools on a real FreeWili 2 over MAIN USB (Windows host, COM port) | **Passed** `info`, `shell`, `log`, `install` (240 KiB in 12 s, SHA-256 verified), `install --replace` with backup, `run` |
| `deploy.py cm0 status` | **Passed**: zones, control lines; `l\u` gave no reply (image lacks runtime USB switching) |
| `deploy.py cm0 reset --force` | **Refused by MAIN** on the test unit: `CM0_RUNPG did not read back held in reset`; CM0 unchanged |
| `deploy.py cm0 power-cycle --force` | **Passed** (zone 17 off/on accepted); did not clear a MAIN-side shell `EBUSY` |
| `wifi_analyzer` panel on the device screen | **Passed**: panel drawn and captured with `gui.screenshot` ("No Wi-Fi adapter" state) |
| `deploy.py cm0 on` (GUI power sequence) | **Passed** after a cold boot: rail and RUN acknowledged, Linux booted |
| `deploy.py cm0 reboot` with boot-id check | **Passed** (the first version's backgrounded reboot died with the session; fixed with `setsid`) |
| USB host mode via `setup_usb_host.py` + reboot | **Passed**: `g_serial` gone; TP-Link Archer T3U (2357:012d) enumerated as `wlan0` |
| `wifi_analyzer` scan on the device | Reports "Wi-Fi radio is off": NetworkManager's radio was disabled and the regulatory domain unset (`00`). A real AP survey is **not yet verified** |
| `wifi_analyzer` keypad stop and launch from the Apps menu on the glass | **Not yet verified** |
| macOS and Linux hosts for these additions | Not yet run; expected through CI |

Hardware findings on the test unit (CM0 Rev 1.0, kernel 6.12.75+rpt-rpi-v8):

- The first `wifi_analyzer` build exited when one `read_buttons()` mailbox
  request timed out while the PC tunnel was active; in isolation the call took
  10 ms. The panel now tolerates lost replies and reconnects (unit-tested).
- `/home/apps` was `root:root 755`, so the console user could not install;
  it was changed to `pi:pi` with the owner's approval, as the README intends.
- `config.txt` carried a host-mode dwc2 line under `[cm5]` and the CM0's
  peripheral line under `[all]`. `setup_usb_host.py` now edits only `[all]`.
- MAIN firmware fix (`rpCM0Comm::linkReset` releases a framed shell session and
  drops an undelivered detach): verified on the device with a MAIN build from
  the firmware working tree. An abandoned session blocked a new one with
  `EBUSY`; after an FPGA-power link reset the new session opened 8.5 s later,
  before the 30 s lease. The fix is not yet in released MAIN firmware.
- After CM0 or MAIN restarts, `fwcm0-bridge` twice failed with `router timeout`
  (FPGA mailbox not answering). An FPGA power cycle fixed it once; a full device
  reset the second time. Root cause not established.
- Opening a shell session immediately after closing one returns `EBUSY` until
  the detach reaches CM0; `fwlink` retries for up to 10 s.
- After a separate OneWili USB session on the same port, one shell session
  stopped draining input and MAIN then answered `EBUSY` to new sessions beyond
  the documented 30-second release. MAIN firmware refuses a new session while
  a previous detach is undelivered; a CM0 power cycle did not clear it, so
  recovery needs a MAIN restart (see [PC deployment](deploy.md#a-stuck-shell-session-ebusy-close-the-active-linux-shell-first)).

The simulated MAIN reproduces the framed-menu protocol used by FreeWili GUI's
Linux Console and fwcom's hardware scripts; it does not model MAIN firmware
timing, flow control, or CM0 shell differences. The OneWili GUI calls in the
Wi-Fi example match the pinned OneWili signatures and the sequence used by a
hardware-validated community integration, but have not been run from this BSP.

Additional [application integration field notes](integration-notes.md) record
2026-09-26 observations with a custom firmware profile and a USB Wi-Fi adapter,
including unresolved USB faults. They are separate from the BSP checks above.
