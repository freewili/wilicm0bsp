"""FreeWili 2 CM0 Wi-Fi analyzer: passive 2.4/5 GHz survey via a USB adapter.

Runs on the CM0 (the FreeWili 2 Linux processor) and is meant to be launched
from **Linux > Apps** with no arguments: it opens a panel on the built-in screen
through the OneWili CM0 API and shows a live 2.4/5 GHz summary, refreshing until
you press Red / keypad X.

Scanning is passive and receive-only. By default it uses `nmcli` (NetworkManager
scans on our behalf, so no root is needed — the Apps launcher runs as the console
user). It reports beacons/probe responses that any station sees; it does not
transmit, associate, deauthenticate, enter monitor mode, stop the fwcm0 bridge,
reset the FPGA, or change boot configuration.

Enabling CM0 USB host mode and plugging in the adapter is a one-time step; see
setup_usb_host.py and README.md.

Console usage (optional):
    python3 app.py                 # panel on the screen (default), live
    python3 app.py --no-display    # print the report to the log instead
    python3 app.py --once          # single scan, print, exit
    python3 app.py --json          # machine-readable JSON to stdout
    python3 app.py --parse-file scan.txt   # analyze saved output, no hardware
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

import analyzer

# USB Wi-Fi adapters validated on the FreeWili 2 CM0 host port (vendor, product).
# Detection also accepts any other interface that exposes an 802.11 phy; this
# list only helps auto-pick the right one when several interfaces are present.
KNOWN_USB_ADAPTERS = {
    ("0846", "9055"): "NETGEAR A6150",
    ("2357", "012d"): "TP-Link Archer T3U",
}

NET = Path("/sys/class/net")


def run(*args: str, check: bool = True, timeout: int = 25) -> subprocess.CompletedProcess:
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout, check=False)
    if check and result.returncode:
        raise RuntimeError(f"{' '.join(args)}: {result.stdout.strip()}")
    return result


def _usb_ids(interface: Path) -> tuple[str, str] | None:
    try:
        device = (interface / "device").resolve()
    except OSError:
        return None
    for parent in [device, *device.parents]:
        vendor, product = parent / "idVendor", parent / "idProduct"
        if vendor.is_file() and product.is_file():
            return (vendor.read_text().strip().lower(), product.read_text().strip().lower())
    return None


def find_adapter(preferred: str | None = None) -> str:
    """Return the Wi-Fi interface to use, or raise a helpful error.

    Prefers an explicit --iface, then a known USB adapter, then any single
    interface that exposes an 802.11 phy.
    """
    if preferred:
        if not (NET / preferred / "phy80211").exists():
            raise RuntimeError(f"{preferred} is not an 802.11 interface")
        return preferred

    wifi = [i for i in NET.iterdir() if (i / "phy80211").exists()]
    if not wifi:
        raise RuntimeError("No Wi-Fi adapter on the CM0 USB host port")

    known = [i.name for i in wifi if _usb_ids(i) in KNOWN_USB_ADAPTERS]
    if len(known) == 1:
        return known[0]
    if len(wifi) == 1:
        return wifi[0].name
    raise RuntimeError("Multiple Wi-Fi interfaces; pass --iface to choose")


# --- Scanning backends ------------------------------------------------------

NMCLI_FIELDS = "SSID,BSSID,CHAN,FREQ,SIGNAL,SECURITY"


def scan_nmcli(iface: str) -> list[analyzer.AccessPoint]:
    """Passive scan via NetworkManager (no root needed)."""
    # A disabled radio or an unavailable device makes nmcli return an empty list
    # with success; say why instead of reporting zero networks.
    radio = run("nmcli", "-t", "radio", "wifi", check=False).stdout.strip()
    if radio == "disabled":
        raise RuntimeError("Wi-Fi radio is off (nmcli radio wifi on)")
    state = run("nmcli", "-t", "-f", "DEVICE,STATE", "device", "status", check=False).stdout
    if f"{iface}:unavailable" in state.splitlines():
        raise RuntimeError(f"{iface} is unavailable to NetworkManager")
    result = run("nmcli", "-m", "multiline", "-f", NMCLI_FIELDS,
                 "device", "wifi", "list", "ifname", iface, "--rescan", "yes",
                 check=False, timeout=30)
    if result.returncode != 0:
        # --rescan may be rate-limited; retry once reading the cached list.
        result = run("nmcli", "-m", "multiline", "-f", NMCLI_FIELDS,
                     "device", "wifi", "list", "ifname", iface,
                     check=False, timeout=30)
        if result.returncode != 0:
            raise RuntimeError(result.stdout.strip() or "nmcli scan failed")
    return analyzer.parse_nmcli(result.stdout)


def scan_iw(iface: str) -> list[analyzer.AccessPoint]:
    """Passive scan via `iw` (needs root; fallback when nmcli is absent)."""
    for _ in range(6):
        result = run("iw", "dev", iface, "scan", check=False, timeout=25)
        if result.returncode == 0:
            return analyzer.parse_scan(result.stdout)
        low = result.stdout.lower()
        if "resource busy" in low or "-16" in low:
            time.sleep(2)
            continue
        if "operation not permitted" in low:
            raise RuntimeError("iw scan needs root; install nmcli or run with sudo")
        raise RuntimeError(f"iw scan failed: {result.stdout.strip()}")
    raise RuntimeError("scan kept returning busy; another process may own the radio")


def choose_backend():
    if shutil.which("nmcli"):
        return scan_nmcli
    if shutil.which("iw"):
        return scan_iw
    raise RuntimeError("Neither nmcli nor iw is installed on this CM0")


def collect(iface: str, backend, passes: int) -> list[analyzer.AccessPoint]:
    scans = []
    for i in range(max(1, passes)):
        scans.append(backend(iface))
        if i + 1 < passes:
            time.sleep(1)
    return analyzer.merge(scans)


# --- Output -----------------------------------------------------------------

def emit(result: dict, aps: list[analyzer.AccessPoint], as_json: bool) -> None:
    print(json.dumps(result, indent=2) if as_json
          else analyzer.format_report(aps, result), flush=True)


def _sleep_or_stop(panel, seconds: int) -> bool:
    """Sleep in 1s slices; return True if the user pressed stop on the panel."""
    for _ in range(max(1, seconds)):
        if panel is not None and panel.should_stop():
            return True
        time.sleep(1)
    return False


def _terminate(*_):
    raise KeyboardInterrupt


def _panel_message(error: Exception) -> tuple[str, str]:
    """Short title/hint pairs that fit the panel's 31-character captions."""
    text = str(error)
    if text.startswith("No Wi-Fi adapter"):
        return "No Wi-Fi adapter", "Plug USB Wi-Fi into CM0 host"
    if text.startswith("Wi-Fi radio is off"):
        return "Wi-Fi radio is off", "nmcli radio wifi on"
    return "Wi-Fi unavailable", text


# --- Main loop --------------------------------------------------------------

def device_loop(args) -> int:
    """Scan → analyze → screen/log, on repeat, with errors shown on the panel."""
    use_display = not args.no_display and not args.json and not args.once
    panel = None
    last_error = None
    try:
        if use_display:
            from display import WifiPanel
            panel = WifiPanel()
            panel.ensure_open()  # shows the panel with "Scanning..." at once

        backend = choose_backend()
        interval = args.watch or 15
        while True:
            if panel is not None:
                panel.ensure_open()  # recovers from a busy or dropped mailbox session
            try:
                iface = find_adapter(args.iface)
                aps = collect(iface, backend, args.passes)
            except Exception as error:  # adapter missing, scan failed, etc.
                if str(error) != last_error:  # log each distinct problem once
                    print(f"wifi_analyzer: {error}", flush=True)
                    last_error = str(error)
                if panel is not None:
                    panel.show_message(*_panel_message(error))
                if args.once:
                    return 1
                if _sleep_or_stop(panel, 5):
                    break
                continue

            if last_error is not None:
                print("wifi_analyzer: scanning", flush=True)
                last_error = None
            result = analyzer.analyze(aps)
            emit(result, aps, args.json)
            if panel is not None:
                panel.update(result)
            if args.once:
                return 0
            if _sleep_or_stop(panel, interval):
                break

        if panel is not None:
            panel.mark_stopped()
            print("Stopped from FreeWili keypad.", flush=True)
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        if panel is not None:
            panel.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iface", help="Wi-Fi interface (default: auto-detect)")
    parser.add_argument("--passes", type=int, default=1,
                        help="scan passes to merge per report (default: 1)")
    parser.add_argument("--watch", type=int, metavar="SECONDS",
                        help="refresh interval in seconds (default: 15)")
    parser.add_argument("--no-display", action="store_true",
                        help="do not use the FreeWili panel; log the text report")
    parser.add_argument("--once", action="store_true",
                        help="single scan, print, and exit (no panel)")
    parser.add_argument("--json", action="store_true",
                        help="print machine-readable JSON (implies --no-display)")
    parser.add_argument("--parse-file", type=Path,
                        help="analyze saved scan output instead of scanning (no root/hardware)")
    args = parser.parse_args()

    # Offline mode: analyze a captured file (iw or nmcli). Runs on any OS.
    if args.parse_file:
        text = args.parse_file.read_text()
        aps = analyzer.parse_nmcli(text) if "SSID:" in text and "BSS " not in text \
            else analyzer.parse_scan(text)
        emit(analyzer.analyze(aps), aps, args.json)
        return 0

    if os.name != "posix" or not NET.exists():
        raise SystemExit("Live scanning runs on the FreeWili 2 CM0 (Linux). "
                         "Use --parse-file to analyze saved output elsewhere.")

    # `deploy.py stop` and service managers send SIGTERM; unwind so the CM0
    # connection is closed instead of being dropped mid-request.
    signal.signal(signal.SIGTERM, _terminate)
    return device_loop(args)


if __name__ == "__main__":
    sys.exit(main())
