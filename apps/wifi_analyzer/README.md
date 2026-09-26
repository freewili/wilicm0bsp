# wifi_analyzer

Passive **2.4 and 5 GHz Wi-Fi analysis** for the FreeWili 2, running on the CM0
(the Linux processor) with a USB Wi-Fi adapter and the OneWili CM0 API for the
built-in display.

It is a **receive-only survey tool**. It performs a passive scan — the same
listening any station does to find networks — then analyzes what it hears and
shows it. By default it scans with `nmcli` so it needs **no root** (the Apps
launcher runs as the console user, and NetworkManager scans on our behalf); it
falls back to `iw` when run as root without NetworkManager. It does **not**
transmit, associate, deauthenticate, use monitor mode, or capture/inject
traffic, and it never stops the `fwcm0` bridge, resets the FPGA, or changes boot
configuration while running.

## What it reports

- Access points per band (2.4 GHz / 5 GHz), with hidden and open-network counts.
- Per-channel occupancy (a text histogram), and the busiest channel per band.
- The strongest network per band, with RSSI, channel, and Wi-Fi generation
  (Wi‑Fi 6 `ax` / `ac` / `n` / legacy).
- The **least-congested 2.4 GHz channel** among 1/6/11, scored by signal-weighted
  overlap so you can pick where to put your own AP.

## Hardware prerequisite (one time)

A USB Wi-Fi adapter only enumerates on the CM0 when its USB port is in host mode.

```sh
sudo python3 setup_usb_host.py     # then reboot the CM0
```

Plug the adapter into the **Linux USB host port**, reboot, and confirm:

```sh
lsusb
iw dev            # should list your wlan interface
```

Validated adapters (auto-detected by USB ID; any interface exposing an 802.11
phy also works, and `--iface` overrides detection):

| Adapter | USB ID |
| --- | --- |
| NETGEAR A6150 | `0846:9055` |
| TP-Link Archer T3U (AC1300) | `2357:012d` |

To revert the USB port later: `sudo python3 setup_usb_host.py --restore` + reboot.

## Install

From a PC connected to MAIN by USB (disconnect FreeWili GUI first), in the BSP
folder � see [PC deployment](../../docs/deploy.md):

```sh
python -m pip install pyserial
python tools/fw.py setup
python tools/deploy.py install wifi_analyzer --run
```

Or on the CM0 itself, from a BSP clone:

```sh
python3 tools/fw.py setup
python3 tools/fw.py install wifi_analyzer
```

## Launch from Linux > Apps (the normal way)

On the FreeWili 2, open **Linux > Apps > wifi_analyzer > run.sh**. With no
arguments it opens a panel on the built-in screen through the OneWili CM0 API
and shows a live 2.4/5 GHz summary — band AP counts, busiest channel per band,
the recommended 2.4 GHz channel, and the strongest network — refreshing every
~15 s. **Press Red or keypad X to stop.** If no adapter is present, the reason
is shown on the screen (there is no terminal when launched from Apps). Text
output is also saved to the app log under `~/.local/state/freewili/apps/`.

## Run from a console (optional)

```sh
python3 tools/fw.py run wifi_analyzer     # same panel behavior, from a shell
python3 apps/wifi_analyzer/app.py --no-display   # text report to the log, no panel
python3 apps/wifi_analyzer/app.py --once         # single scan, print, exit
python3 apps/wifi_analyzer/app.py --json         # machine-readable JSON
```

No `sudo` is required when NetworkManager is present (the default). Use Ctrl-C
to stop a console run.

## Analyze saved output anywhere (no device)

```sh
# either backend's output works; the parser auto-detects the format
nmcli -m multiline -f SSID,BSSID,CHAN,FREQ,SIGNAL,SECURITY dev wifi list > scan.txt
iw dev wlan0 scan > scan.txt
python3 app.py --parse-file scan.txt --json
```

## Troubleshooting

- Screen stays on the previous app: read the log from the PC with
  `python tools/deploy.py log wifi_analyzer`.
- "No Wi-Fi adapter on the CM0 USB host port": check `lsusb` and `iw dev`
  (`python tools/deploy.py shell "lsusb; iw dev"`), and that host mode is enabled.
- An adapter that enumerates but cannot start its radio (kernel log
  `status: -71`, `mac power on failed`) is a known USB fault on some units; see
  [integration notes](../../docs/integration-notes.md). Unplug/replug the adapter.
- "Wi-Fi radio is off": NetworkManager's radio is disabled, so scans return
  nothing. Enable it once with `nmcli radio wifi on` (the analyzer never
  changes this for you).
- Scan errors from `nmcli`: confirm NetworkManager manages the adapter
  (`nmcli device status`). The analyzer never takes the adapter away from it.
- Few or no 5 GHz networks: set the CM0's regulatory country
  (`sudo iw reg set US`, or your country code; persist it with
  `sudo raspi-config nonint do_wifi_country US`). With the world domain `00`, most 5 GHz
  channels are passive-scan only.

## Files

| File | Purpose |
| --- | --- |
| `app.py` | Entry point: detect adapter, scan, analyze, print, optional live panel |
| `analyzer.py` | Pure scan parsing + analysis (no I/O; unit-tested cross-platform) |
| `display.py` | OneWili CM0 panel rendering (imported only with `--display`) |
| `setup_usb_host.py` | One-time CM0 USB host-mode switch (run by hand, with sudo) |

## Legality

Passive scanning of the airwaves is broadly permitted, but you are responsible
for local rules. Set your Wi-Fi country on the CM0 (`iw reg set <CC>`) so 5 GHz
channels are surveyed within your regulatory domain.

## Attribution

The USB host-mode switch and USB-ID adapter detection are adapted from the
community FreeWili CM0 Wi-Fi driver
[gothcawili](https://github.com/evaderkrub/gothcawili). Only its passive,
non-offensive infrastructure is reused here; this app implements analysis only.
