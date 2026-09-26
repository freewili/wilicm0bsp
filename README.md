# WiliCM0BSP

Build Linux applications for the **CM0 compute module in FreeWili 2** with Python
or C++. This board support package includes the CM0 mailbox/SPI/UART driver,
examples, application tools, and a pinned
[OneWili](https://github.com/freewili/onewili) submodule for controlling MAIN.
The CM0 is the Linux processor (BCM2837/CM3 class); it is not an ARM Cortex-M0.
DISPLAY processor applications use [WiliBSP](https://github.com/freewili/wilibsp).

**Put your Linux apps in `/home/apps/` for easy launching from Linux > Apps on
the FreeWili 2.** Install each app into its own folder and select `run.sh`.
This is on the CM0 Linux filesystem. The MAIN SD card's `/apps/` folder serves
DISPLAY apps and is a separate location.

## Start on the CM0

Use a current FreeWili 2 Linux image with a working `fwcm0-bridge` service and
matching MAIN firmware (mailbox protocol 1.2 or newer). Enable Linux/CM0 and
FPGA power through the device's power controls, then wait for Linux to boot.
Open the Linux Console in FreeWili GUI, the device terminal, or an SSH session
you have configured. The commands below run **inside CM0 Linux**.

```sh
sudo apt-get update
sudo apt-get install -y git python3 python3-pip
git clone --recurse-submodules https://github.com/freewili/wilicm0bsp.git
cd wilicm0bsp
python3 tools/fw.py setup
python3 tools/fw.py doctor
python3 tools/fw.py run hello_python
```

The setup command installs a local Python runtime in `.runtime/`. It leaves
system Python alone and needs no root access. OneWili uses `fwcm0 api` and the
running bridge. No USB connection to a PC is required for the application.
If `doctor` reports an old driver, follow [the driver upgrade steps](docs/driver.md).

If `/home/apps` does not yet exist, create it for your Linux login user:

```sh
sudo install -d -o "$(id -un)" -g "$(id -gn)" /home/apps
python3 tools/fw.py install hello_python
```

On the device, open **Linux > Apps > hello_python > run.sh** and launch it.
The installed folder includes its Python dependencies, so it keeps working
after moving or deleting this source checkout. Launch output is saved under
`~/.local/state/freewili/apps/app-*.log` for the launcher user.

```sh
ls -t ~/.local/state/freewili/apps/app-*.log | head
```

Do not run the installer with sudo. If an existing apps folder is not writable,
ask its owner to grant your user access. Installation refuses to replace an
existing app. `--apps-dir <path>` creates a staging folder for copying to CM0.

## Make an application

```sh
python3 tools/fw.py new-app my_app
# Edit apps/my_app/app.py
python3 tools/fw.py run my_app
python3 tools/fw.py install my_app
```

```python
from onewili_cm0 import connect_cm0

with connect_cm0() as device:
    print(device.hardware.system.device_state().unwrap())
```

Use `.unwrap()` or check each Result for errors. Close the connection when your
app finishes. One CM0 API/interactive-console session can own the mailbox
console channel at a time; MAIN USB commands and the Linux shell can run
concurrently. A second app receives a busy error.

| Example | Purpose |
| --- | --- |
| `apps/hello_python` | Read MAIN Device State; no hardware settings changed |
| `apps/gpio_poll` | Ten GPIO snapshots; FPGA power must be on |
| `apps/device_info` | Native C++ app using generated OneWili C calls over the bridge |
| `apps/template` | Minimal Python starter copied by `new-app` |
| `drivers/fwcm0/examples` | Lower-level router examples; read the driver guide first |

## C++ applications

Build on CM0 to get the correct Linux executable architecture:

```sh
sudo apt-get install -y build-essential cmake
cmake -S . -B build -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/apps/device_info/device_info
python3 tools/fw.py install device_info
```

Include `wilicm0/device.hpp`, construct `wilicm0::Device`, and pass `device.get()`
to the generated `ow_*` functions. Link the CMake target `wilicm0::wilicm0`.
Copy `apps/device_info` to start another native app, rename its CMake target,
and add it with `add_subdirectory(apps/<name>)` in the root CMake file.
The adapter connects to `/run/fwcm0-bridge.sock`; it does not stop the bridge
or take over SPI/UART. See [the architecture guide](docs/architecture.md).

## Driver, agents, and verification

- [Driver setup and troubleshooting](docs/driver.md): rebuild `fwcm0`, systemd,
  permissions, and hardware ownership.
- [Application and launcher contract](docs/apps.md): launch behavior, logs,
  storage, deployment, and standalone app repositories.
- [AGENTS.md](AGENTS.md): instructions for coding agents and contributors;
  [agent references](agents/README.md) cover development and hardware limits.
- [Platform and verification status](docs/platform-support.md).
- [OneWili reference](libs/onewili/docs/index.md) and
  [CM0 transport details](libs/onewili/cm0/README.md).

CM0 supports menu commands and framed file upload/download to MAIN's SD card.
The FTDI binary event stream (including logic-analyzer captures) and USB
directory-list events are **not carried by the CM0 mailbox**. Use the OneWili
USB packages on a connected host for those features. CAN receive can use the
existing polled receive command on CM0.

License: [MIT](LICENSE). Third-party dependencies retain their own licenses;
see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Downloads

[Releases](https://github.com/freewili/wilicm0bsp/releases) provide a source
archive that includes the pinned OneWili sources, plus ready-to-copy example
app folders. GitHub's automatic source ZIP omits Git submodules; use the
attached `wilicm0bsp-*-source.tar.gz` or clone with `--recurse-submodules`.
Extract an example app archive into `/home/apps/` on CM0 and select its
`run.sh` in Linux > Apps. Native example binaries target ARM64 Linux.
