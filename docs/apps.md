# Linux application contract

Install apps under **`/home/apps/<name>/`**, with an executable `run.sh` entry
point. The FreeWili 2 **Linux > Apps** browser starts at `/home/apps/`.
It shows folders, `.py`, `.sh`, and executable files. Enter your app folder
and launch `run.sh` to use the bundled Python dependencies.

The launcher runs the selected file as the Linux console user, with the file's
parent as working directory. Standard input is disconnected. Standard output
and error go to a unique log under `~/.local/state/freewili/apps/`. The launch
dialog reports a process ID and log name; successful spawning does not prove
the app completed. Check its log. Avoid `input()` and terminal-only prompts.

The Python installer bundles the pinned OneWili Python sources, CM0 adapter,
and `result` dependency into the app's `.lib/`. Its launcher resolves paths
relative to itself. A source checkout, virtualenv activation, root login, or
PC is not needed at launch. Use short names; the firmware browser omits paths
longer than 126 encoded bytes and limits each listing to 512 entries.

Keep app-owned data under `~/.local/share/<app-name>/` and configuration under
`~/.config/<app-name>/`. Create them as needed. Treat the app folder as code.
MAIN SD paths used by OneWili file transfers are separate from Linux paths.

## Use the built-in screen through OneWili

Apps launched from Linux > Apps have no terminal. An interactive app should use
the FreeWili 2 screen through the OneWili CM0 GUI API — the same `connect_cm0()`
connection used for hardware access — and must be fully usable when `run.sh`
is started with no arguments:

```python
from onewili_cm0 import connect_cm0

BG, FG = 0x101820, 0xE8EEF2
with connect_cm0() as device:
    gui = device.gui
    gui.panels.show_panel(0).unwrap()
    gui.panels.add_panel(False, 0, BG, True).unwrap()        # custom panel with menu
    gui.panels.set_menu_text(4, "Stop").unwrap()             # 0 gray … 4 red
    gui.controls.add_text(0, 20, 20, 0, 0, FG, BG, "Starting...").unwrap()
    while not gui.panels.read_buttons().unwrap() & ((1 << 4) | (1 << 11)):  # red or keypad X
        gui.control_properties.set_control_value_text(0, "Working").unwrap()
        ...
```

- **Draw on startup.** Open the panel before slow work so the user sees the app
  started. Command-line options may add console behavior but must not be needed.
- **Show status and errors on the screen.** A missing peripheral or permission
  problem should appear on the panel and in the log, and the app should keep
  retrying or wait for Stop rather than exiting silently.
- **Provide a stop.** Poll `read_buttons()` (bits 0–4 gray…red, keypad X = bit 11)
  on your refresh loop. The press latch is shared by all API clients; use one
  polling consumer and repaint only captions that changed.
- **Do not require root.** The launcher runs as the console user. Prefer
  unprivileged interfaces (for example NetworkManager's `nmcli` for Wi-Fi scans).
- Close the connection on exit. Only one CM0 API session is supported at a time.

`apps/wifi_analyzer` is a complete example that follows these rules.

## Transfer from a development computer

From a PC connected to MAIN over USB, `python tools/deploy.py install <app>`
stages the app and copies it straight into `/home/apps/<app>/` through MAIN's
Linux shell tunnel; see [PC deployment](deploy.md). It needs pyserial and a
free MAIN serial port (disconnect FreeWili GUI), but no network or SSH.

To copy by hand instead, use `python tools/fw.py install hello_python --apps-dir dist/apps` to stage a
self-contained app after running `setup`. Copy `dist/apps/hello_python` into
`/home/apps/` using FreeWili GUI's Linux file tools or configured SSH/SCP.
On CM0, run `chmod +x /home/apps/hello_python/run.sh`; copied native binaries
also need the executable bit. A Windows/macOS native executable cannot run
on CM0: build native apps on CM0 or use a matching Linux ARM64 toolchain/sysroot.

Installation refuses to overwrite existing apps. Stop an app before replacing
its folder, preserve its user data, and deliberately remove or rename the old
code folder. Each installed Python app keeps the dependency version it shipped
with; updating the BSP checkout alone does not update installed apps.

## Standalone repositories

Pin this BSP as a submodule. A Python app can use the same setup/install pattern;
for C++, add the BSP with `add_subdirectory`, set `BUILD_TESTING=OFF` for consumer
builds, and link `wilicm0::wilicm0`. Keep your app target separate from BSP code.
Add an `AGENTS.md` linking to the pinned BSP's guide, documenting power/peripheral
ownership, your launch command, and tests. Publish a relocatable app folder with
your release, including dependency licenses and the target architecture.
