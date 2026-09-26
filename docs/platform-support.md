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

Additional [application integration field notes](integration-notes.md) record
2026-09-26 observations with a custom firmware profile and a USB Wi-Fi adapter,
including unresolved USB faults. They are separate from the BSP checks above.
