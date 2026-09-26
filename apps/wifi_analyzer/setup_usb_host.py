"""One-time helper: switch the FreeWili 2 CM0 USB port to host mode.

A USB Wi-Fi adapter only enumerates on the CM0 when its USB port is in host
mode. This edits only the CM0 USB lines in the ``[all]`` sections of
``/boot/firmware/config.txt`` (the dwc2 overlay and the GPIO2 mux) and the dwc2
module list in ``cmdline.txt``, backing up both files first. Lines in other
board sections such as ``[cm4]``/``[cm5]`` and every other setting are left
alone. Run it once by hand with sudo, then reboot; it is NOT part of the
analyzer app's startup (apps must not change boot configuration).

    sudo python3 setup_usb_host.py            # enable USB host mode
    sudo python3 setup_usb_host.py --restore  # back to gadget/peripheral mode

Host mode disables the CM0 USB gadget serial console. Keep another access path:
FreeWili GUI's Linux Console and ``tools/deploy.py`` reach CM0 through MAIN.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
from pathlib import Path

SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
GADGET = {"overlay": "dtoverlay=dwc2,dr_mode=peripheral", "gpio2": "gpio=2=op,dl"}
HOST = {"overlay": "dtoverlay=dwc2,dr_mode=host", "gpio2": "gpio=2=op,dh"}
GPIO3 = "gpio=3=op,dh"  # held high in both modes on FreeWili 2


def all_section_lines(text: str) -> list[str]:
    """Stripped lines that apply to every board (before any filter, or in [all])."""
    section, lines = "all", []
    for line in text.splitlines():
        match = SECTION.match(line)
        if match:
            section = match.group(1).strip().lower()
        elif section == "all":
            lines.append(line.strip())
    return lines


def _switch(text: str, source: dict, target: dict) -> str:
    active = all_section_lines(text)
    if target["overlay"] in active and target["gpio2"] in active and source["overlay"] not in active:
        return text  # already in the requested mode
    for line in (source["overlay"], source["gpio2"], GPIO3):
        if active.count(line) != 1:
            raise ValueError("Unrecognized USB mux configuration in [all] "
                             f"(expected one '{line}'); review boot config by hand")
    edits = {source["overlay"]: target["overlay"], source["gpio2"]: target["gpio2"]}
    section, out = "all", []
    for line in text.splitlines(keepends=True):
        match = SECTION.match(line)
        if match:
            section = match.group(1).strip().lower()
        elif section == "all" and line.strip() in edits:
            line = line.replace(line.strip(), edits[line.strip()])
        out.append(line)
    return "".join(out)


def host_config(text: str) -> str:
    return _switch(text, GADGET, HOST)


def gadget_config(text: str) -> str:
    return _switch(text, HOST, GADGET)


def usb_cmdline(text: str, gadget: bool = False) -> str:
    pattern = r"(?<!\S)modules-load=dwc2(?:,g_serial)?(?=\s|$)"
    replacement = "modules-load=dwc2" + (",g_serial" if gadget else "")
    result, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise ValueError("Unrecognized USB module boot setting")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--restore", action="store_true", help="revert to gadget/peripheral mode")
    args = parser.parse_args()

    if os.geteuid() != 0:
        raise SystemExit("Run with sudo")
    model = Path("/proc/device-tree/model")
    if not model.exists() or "Compute Module 0" not in model.read_text():
        raise SystemExit("This configuration is for the FreeWili 2 with Raspberry Pi CM0")

    root = Path("/boot/firmware")
    backup = root / "wifi-analyzer-usb-backup"
    convert = gadget_config if args.restore else host_config
    original_config = (root / "config.txt").read_text()
    original_cmdline = (root / "cmdline.txt").read_text()
    config = convert(original_config)
    cmdline = usb_cmdline(original_cmdline, args.restore)
    if config == original_config and cmdline == original_cmdline:
        print("Already in %s mode; nothing changed." % ("gadget" if args.restore else "host"))
        return

    backup.mkdir(exist_ok=True)
    for name in ("config.txt", "cmdline.txt"):
        if not (backup / name).exists():
            shutil.copy2(root / name, backup / name)
    (root / "config.txt").write_text(config)
    (root / "cmdline.txt").write_text(cmdline)
    os.sync()

    if args.restore:
        print("USB gadget/peripheral mode restored; other boot settings kept. Reboot the CM0.")
    else:
        print("USB host mode configured (dwc2 host, GPIO2=1, GPIO3=1). Reboot the CM0,")
        print("plug the adapter into the Linux USB host port, then check with lsusb.")


if __name__ == "__main__":
    main()
