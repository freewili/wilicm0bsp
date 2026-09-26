# CM0 driver setup and troubleshooting

A current FreeWili Linux image should already include `fwcm0` and the
`fwcm0-bridge` systemd service. Start by checking:

```sh
systemctl status fwcm0-bridge --no-pager
fwcm0 help
fwcm0 status
ls -l /run/fwcm0-bridge.sock
```

The socket is normally `root:dialout`, mode `0660`. Add your application user
to `dialout` if needed, then log out and back in. Do not make the socket world
writable. If another API/interactive-console app owns the channel, close it
before starting another. The Linux shell itself can remain open.
`fwcm0 help` must list `api`; older images may only have `console` and need
the driver upgrade below. After Python setup, `python3 tools/fw.py doctor`
checks the CLI and an actual read-only MAIN response.

## Rebuild the driver when needed

Use Debian/Raspberry Pi OS Trixie with **libgpiod 2.x**. Bookworm's libgpiod 1.x
is incompatible with this hardware driver. Build on the CM0:

```sh
sudo apt-get install -y build-essential cmake pkg-config libgpiod-dev
cmake -S . -B build-driver -DWILICM0_BUILD_DRIVER=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-driver --parallel 2
```

For installation, use an SSH or gadget-serial connection independent of the
bridge; stopping the bridge terminates the mailbox Linux Console connection.
This maintenance sequence replaces the installed driver with the built one:

```sh
sudo systemctl stop fwcm0-bridge
sudo cmake --install build-driver
sudo ldconfig
sudo install -m 0644 config/fwcm0-bridge.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now fwcm0-bridge
```

The service uses the driver's default 7,812,500 baud unless `FWCM0_BAUD` in
`/etc/environment` overrides it. The reference boot settings are in
`config/config-fwcm0.txt`; a supported image already configures these pins and
UART clock. App setup does not change them. Do not blindly append duplicates
to a working image's configuration.

If status reports bad responses or timeouts, check FPGA/CM0 power, wait for
MAIN startup, and confirm matching firmware, gateware, and Linux image.
Review `journalctl -u fwcm0-bridge -n 50 --no-pager`. An active service alone
does not establish a healthy hardware link.

The portable driver core and tests work without hardware on Windows/macOS/Linux.
The SPI/UART transport and bridge require Linux and the actual FreeWili wiring.
The copied driver sources are maintained in this repository; changes here
must keep its socket and mailbox protocols compatible with shipping firmware.
