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
| OneWili Python, C and Rust regression suites | Passed Windows, macOS and Linux CI |
| BSP macOS/Linux host builds and socket tests | CI configured; initial run pending |
| Linux libgpiod 2.x driver build | CI configured; initial run pending |
| CM0 ARM64 build and real mailbox examples | In progress before release |

CI exercises the socket adapter against a simulated local bridge; it cannot
validate SPI/UART pins, power sequencing or FPGA behavior. Real device results
and any remaining gaps will be recorded here before the first release.

FTDI binary event forwarding and USB directory-list events are not implemented
on CM0. Firmware peripheral behavior beyond the examples is not exhaustively
tested. The GUI plugin ABI is not part of this BSP and is unchanged.
