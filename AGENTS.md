# WiliCM0BSP agent guide

Read this file and the linked guide relevant to your change before editing.
This repository builds Linux user-space applications for the FreeWili 2 CM0.
It does not build RP2350 firmware or DISPLAY UF2 applications.

- `apps/`: customer apps. Start with `hello_python`, `device_info`, or `template`.
- `bsp/`: C++ OneWili bridge socket adapter. The bridge keeps hardware ownership.
- `drivers/fwcm0/`: portable protocol/router core, Linux hardware transport,
  CLI and bridge, examples, and mock-based tests.
- `libs/onewili/`: pinned public Git submodule. Never hand-edit generated bindings.
- `tools/fw.py`: Python setup, scaffolding, running and installation.
- `tools/deploy.py`, `tools/fwlink.py`: PC-side install/run/log over the MAIN USB
  Linux shell tunnel. Never replay a timed-out shell or menu write.

Read [development](agents/development.md), [hardware and transports](agents/hardware.md),
and [the app contract](docs/apps.md). Keep app instructions and examples runnable
from a fresh recursive clone with no private repositories or absolute build paths.

Apps belong in **`/home/apps/<app-name>/` on CM0 Linux**, with a `run.sh` entry.
This is the folder the Linux Apps launcher opens. An app must work when `run.sh` is launched with
no arguments and no terminal; interactive apps use the screen through OneWili
(see [the app contract](docs/apps.md)). Installed apps must not rely
on the build checkout or an interactive terminal. Put persistent app data under
the launch user's `~/.local/share/<app-name>/`, not alongside executables.

Normal applications use the running bridge. Do not stop it, reset the FPGA,
change boot configuration, or replace firmware as part of app startup. Check
every command result; handle busy/disconnected sessions and close connections.
Never retry an ambiguous hardware write automatically. Enable only the power
zones a feature requires and restore app-owned temporary hardware settings.

Do not claim CM0 binary streaming: FTDI events and USB directory-list events
are not routed over the mailbox. Prefer request/reply or supported polling.
Only one CM0 API/interactive-console session is supported at a time.

For changes, run the relevant Python tests plus CMake/CTest. Driver core and
tools must remain usable on Windows, macOS and Linux; hardware runtime is Linux.
Record actual platform and device checks in `docs/platform-support.md`. Host
tests are not hardware verification. Keep unavailable environments explicit.

Use feature branches and pull requests for updates. Do not rewrite published
history. Submodule updates must use a published OneWili commit and pass these
tests. Preserve dependency licenses. Keep credentials and device/user-specific
configuration out of published files.
