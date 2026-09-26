#!/usr/bin/env python3
"""Install and run CM0 apps from a PC over the FreeWili 2 MAIN USB cable.

Builds the same self-contained app folder as ``fw.py install`` on the PC, copies
it to ``/home/apps/<app>/`` on the CM0 through MAIN's Linux shell tunnel, and can
start it or show its log. No network, SSH, or CM0 Gadget Serial is needed.

    python tools/deploy.py ports
    python tools/deploy.py install wifi_analyzer [--replace] [--run]
    python tools/deploy.py log wifi_analyzer
    python tools/deploy.py stop wifi_analyzer
    python tools/deploy.py shell "lsusb"

Requires ``pip install pyserial`` and ``python tools/fw.py setup`` on the PC.
Disconnect FreeWili GUI first: only one program can own the MAIN serial port.
"""
from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import re
import secrets
import shlex
import sys
import tempfile
import time
import zipfile

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import fwlink  # noqa: E402

_spec = importlib.util.spec_from_file_location("fw", TOOLS / "fw.py")
fw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fw)

APPS_DIR = "/home/apps"
LOG_DIR = "~/.local/state/freewili/apps"


def stage_zip(name: str, build: Path) -> bytes:
    """Stage ``name`` exactly like ``fw.py install`` and return it as a zip."""
    with tempfile.TemporaryDirectory(prefix="fw-deploy-") as temp:
        with contextlib.redirect_stdout(io.StringIO()):  # fw prints on-device hints
            fw.install_app(name, temp, build)
        staged = Path(temp) / name
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(staged.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(staged).as_posix())
        return packed.getvalue()


def remote_extract_script(name: str, zip_path: str, nonce: str, replace: bool) -> str:
    """Python run on CM0: unpack beside /home/apps, then publish with one rename."""
    return (
        "import os,pathlib,shutil,time,zipfile\n"
        f"apps=pathlib.Path({APPS_DIR!r});name={name!r};z=pathlib.Path({zip_path!r})\n"
        f"stage=apps/('.fw-deploy-'+{nonce!r});target=apps/name\n"
        "try:\n"
        "  stage.mkdir()\n"
        "  zipfile.ZipFile(z).extractall(stage)\n"
        "  for exe in (stage/'run.sh',stage/name):\n"
        "    if exe.is_file(): exe.chmod(0o755)\n"
        "  if target.exists():\n"
        f"    if not {replace!r}: raise SystemExit(str(target)+' already exists; use --replace')\n"
        "    backups=pathlib.Path.home()/'.local/share/fw-deploy/backups';backups.mkdir(parents=True,exist_ok=True)\n"
        "    old=backups/(name+'-'+time.strftime('%Y%m%d-%H%M%S'))\n"
        "    shutil.move(str(target),str(old));print('BACKUP',old)\n"
        "  os.rename(stage,target);print('INSTALLED',target)\n"
        "finally:\n"
        "  shutil.rmtree(stage,ignore_errors=True);z.unlink(missing_ok=True)\n"
    )


def ensure_apps_dir(shell: fwlink.LinuxShell) -> None:
    code, _ = shell.run(f"test -d {shlex.quote(APPS_DIR)}")
    if code:
        # Same one-time step the README gives; never chowns an existing folder.
        code, out = shell.run(f'sudo -n install -d -o "$(id -un)" -g "$(id -gn)" {shlex.quote(APPS_DIR)}')
        if code:
            raise fwlink.LinkError(
                f"{APPS_DIR} does not exist and could not be created without a password.\n"
                f'On the CM0 run once: sudo install -d -o "$(id -un)" -g "$(id -gn)" {APPS_DIR}\n{out}')
    code, _ = shell.run(f"test -w {shlex.quote(APPS_DIR)}")
    if code:
        raise fwlink.LinkError(f"{APPS_DIR} is not writable by the CM0 console user; "
                               "ask its owner to grant access.")


def app_pattern(name: str) -> str:
    # Launched apps run "$APP_DIR/app.py" or "$APP_DIR/<name>" by absolute path.
    return shlex.quote(f"{APPS_DIR}/{name}/")


def stop_app(shell: fwlink.LinuxShell, name: str) -> bool:
    code, _ = shell.run(f"pgrep -f -- {app_pattern(name)} >/dev/null")
    if code:
        return False
    shell.run(f"pkill -TERM -f -- {app_pattern(name)}")
    shell.run(f"for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -f -- {app_pattern(name)} >/dev/null || break; sleep 0.5; done")
    return True


def start_app(shell: fwlink.LinuxShell, name: str) -> str:
    """Start run.sh detached, logging where the Apps launcher logs."""
    log = f"{LOG_DIR}/app-{name}-$(date +%Y%m%d-%H%M%S).log"
    app = shlex.quote(f"{APPS_DIR}/{name}")
    # Background only the app, in a subshell, so this shell keeps its cwd/state.
    _, out = shell.run(
        f"L={log}; mkdir -p {LOG_DIR}; "
        f"if [ -x {app}/run.sh ]; then "
        f'(cd {app} && setsid nohup ./run.sh > "$L" 2>&1 < /dev/null &); echo "Started {name}; log $L"; '
        f"else echo {app}'/run.sh is not installed'; false; fi", check=True)
    return out.strip()


def show_log(shell: fwlink.LinuxShell, name: str | None, lines: int) -> str:
    pick = (f"ls -t {LOG_DIR}/app-{name}-*.log {LOG_DIR}/app-*.log 2>/dev/null | head -1" if name
            else f"ls -t {LOG_DIR}/app-*.log 2>/dev/null | head -1")
    _, out = shell.run(f'f=$({pick}); if [ -n "$f" ]; then echo "== $f"; tail -n {lines} "$f"; '
                       f'else echo "No app logs in {LOG_DIR} yet."; fi')
    return out


def cmd_install(args, shell_factory) -> int:
    name = fw.app_name(args.app)
    print(f"Staging {name} ...", flush=True)
    data = stage_zip(name, Path(args.build))
    nonce = secrets.token_hex(4)
    # Hidden (the Apps browser skips it), beside the target so publishing is one rename.
    zip_path = f"{APPS_DIR}/.fw-deploy-{nonce}.zip"
    with shell_factory() as shell:
        ensure_apps_dir(shell)
        if args.replace and stop_app(shell, name):
            print(f"Stopped running {name}.", flush=True)

        def progress(done, total):
            print(f"\rCopying {len(data) // 1024} KiB to CM0: {100 * done // total:3d}%", end="", flush=True)

        started = time.monotonic()
        shell.upload(data, zip_path, progress)
        print(f"  ({time.monotonic() - started:.0f}s)", flush=True)
        code, out = shell.python(remote_extract_script(name, zip_path, nonce, args.replace))
        print(out, flush=True)
        if code:
            return code
        print(f"Open Linux > Apps > {name} > run.sh on the FreeWili 2.", flush=True)
        if args.run:
            print(start_app(shell, name), flush=True)
            time.sleep(3)
            print(show_log(shell, name, 40), flush=True)
    return 0


BOOT_ID = "cat /proc/sys/kernel/random/boot_id"


def wait_for_linux(shell_factory, timeout: float = 240.0, old_boot: str | None = None) -> bool:
    """Poll until a CM0 shell answers; with old_boot, also require a new boot."""
    # While CM0 boots, a probe that got no answer is closed, and MAIN keeps that
    # detach queued until the bridge starts, so EBUSY is expected for a while.
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            with shell_factory() as shell:
                _, boot = shell.run(BOOT_ID, check=True, timeout=15)
            if old_boot is None or boot.strip() != old_boot:
                return True
            last = "CM0 has not restarted yet (same boot id)"
        except (fwlink.LinkError, TimeoutError) as error:
            last = error
        time.sleep(5)
    if last is not None:
        print(f"Last error: {last}", file=sys.stderr)
    return False


def cmd_cm0(args, menu_factory, shell_factory) -> int:
    """CM0 power, reset and USB-mode control through MAIN (no CM0 shell needed)."""
    if args.action == "status":
        menu = menu_factory()
        try:
            menu.wait_ready()
            print("power zones:  ", menu.call("h\\p\\g"))
            print("control lines:", menu.call("h\\p\\n"), " (19 = CM0 RUN)")
            try:
                print("CM0 USB mode: ", menu.call("l\\u status", timeout=10))
            except (fwlink.LinkError, TimeoutError) as error:
                print("CM0 USB mode:  unavailable on this image/firmware:", error)
        finally:
            menu.close()
        return 0

    if args.action == "on":
        menu = menu_factory()
        try:
            menu.wait_ready()
            zones = menu.call("h\\p\\g")
            state = dict(re.findall(r"\b(6|17|19):\S+\s+(on|off|released|held)", zones))
            if len(state) != 3:
                raise fwlink.LinkError(f"Unrecognized power-zone reply: {zones}")
            changed = False
            if state["6"] != "on":  # the mailbox is FPGA fabric
                print("FPGA power (zone 6) on ...", flush=True)
                menu.call("h\\p\\s 6 1", timeout=15)
                time.sleep(2)
                changed = True
            if state["17"] != "on" or state["19"] != "released":
                # FreeWili GUI's Linux power sequence: rail first, acknowledged by the
                # PIC, let its zone walk finish, then release RUN. Separate frames sent
                # back to back can overwrite each other; RUN must never rise unpowered.
                print("CM0 power (zone 17) on ...", flush=True)
                menu.call("h\\p\\s 17 1", timeout=15)
                time.sleep(2)
                print("Releasing CM0 RUN (line 19) ...", flush=True)
                menu.call("h\\p\\c 1", timeout=15)
                changed = True
            if not changed:
                print("FPGA and CM0 are already on.", flush=True)
        finally:
            menu.close()
        if changed:
            time.sleep(15)
        if wait_for_linux(shell_factory):
            print("CM0 Linux is up.")
            return 0
        print("CM0 did not answer within 4 minutes; check `cm0 status`.", file=sys.stderr)
        return 1

    if args.action == "usb":
        menu = menu_factory()
        try:
            menu.wait_ready()
            # Runtime switch only; the boot default is unchanged. Not replayed on timeout.
            print("CM0 USB mode:", menu.call(f"l\\u {args.mode}", timeout=15))
        finally:
            menu.close()
        return 0

    old_boot = None
    if args.action == "reboot":
        # Clean path: Linux unmounts filesystems itself.
        with shell_factory() as shell:
            code, out = shell.run("sudo -n true")
            if code:
                raise fwlink.LinkError("Clean reboot needs passwordless sudo for the CM0 user:\n" + out)
            old_boot = shell.run(BOOT_ID, check=True)[1].strip()
            # setsid: closing this shell session must not kill the pending reboot.
            shell.run("setsid -f sh -c 'sleep 2; sudo -n systemctl reboot' >/dev/null 2>&1",
                      check=True)
        print("CM0 rebooting cleanly; waiting for Linux ...", flush=True)
        time.sleep(20)
    elif args.action == "power-cycle":
        if not args.force:
            raise fwlink.LinkError(
                "power-cycle switches CM0 power (zone 17) off and on without a Linux shutdown; "
                "the SD card may be mid-write. Use `cm0 reboot` when the shell works; pass "
                "--force only when it does not.")
        menu = menu_factory()
        try:
            menu.wait_ready()
            print("Switching CM0 power (zone 17) off without a Linux shutdown ...", flush=True)
            menu.call("h\\p\\s 17 0", timeout=15)
            time.sleep(5)
            try:
                menu.call("h\\p\\s 17 1", timeout=15)
            except (fwlink.LinkError, TimeoutError) as error:
                raise fwlink.LinkError(
                    f"CM0 power may still be off ({error}). Check `cm0 status`; switch zone 17 "
                    "on from FreeWili GUI power controls.")
        finally:
            menu.close()
        print("CM0 power on; waiting for Linux to boot ...", flush=True)
        time.sleep(15)
    else:  # reset
        if not args.force:
            raise fwlink.LinkError(
                "reset holds the CM0 RUN line: like pulling power, Linux does not shut down "
                "and the SD card may be mid-write. Use `cm0 reboot` when the shell works; "
                "pass --force only when it does not (for example a stuck shell session).")
        print("Holding CM0 in reset (RUN line, zone 19) without a Linux shutdown ...", flush=True)
        menu = menu_factory()
        try:
            menu.wait_ready()
            menu.call("h\\p\\c 0", timeout=15)
            time.sleep(2)
            try:
                menu.call("h\\p\\c 1", timeout=15)
            except (fwlink.LinkError, TimeoutError) as error:
                raise fwlink.LinkError(
                    f"CM0 may still be held in reset ({error}). Check with `cm0 status`; "
                    "release it from FreeWili GUI power controls or run `cm0 reset --force` again.")
        finally:
            menu.close()
        print("Released; waiting for Linux to boot ...", flush=True)
        time.sleep(15)

    if wait_for_linux(shell_factory, old_boot=old_boot):
        print("CM0 Linux is back.")
        return 0
    print("CM0 did not come back within 4 minutes. Check `cm0 status`, Linux/CM0 power, and "
          "whether another Linux shell (FreeWili GUI) holds the session.", file=sys.stderr)
    return 1


def main(argv=None, shell_factory=None, menu_factory=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="MAIN USB serial port (default: auto-detect)")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ports", help="List FreeWili 2 MAIN serial ports")
    sub.add_parser("info", help="Show CM0 Linux details and installed apps")
    install = sub.add_parser("install", help="Copy an app into /home/apps on the CM0")
    install.add_argument("app")
    install.add_argument("--replace", action="store_true",
                         help="stop and back up an existing install, then replace it")
    install.add_argument("--run", action="store_true", help="start it after installing")
    install.add_argument("--build", default=str(fw.ROOT / "build"),
                         help="CMake build folder for native apps")
    for verb, text in (("run", "Start an installed app (as Linux > Apps would)"),
                       ("stop", "Stop a running app")):
        sub.add_parser(verb, help=text).add_argument("app")
    log = sub.add_parser("log", help="Print the newest app log")
    log.add_argument("app", nargs="?")
    log.add_argument("-n", "--lines", type=int, default=80)
    shell_cmd = sub.add_parser("shell", help="Run one shell command on the CM0")
    shell_cmd.add_argument("words", nargs="+")
    cm0 = sub.add_parser("cm0", help="CM0 power, reset and USB mode through MAIN")
    cm0_sub = cm0.add_subparsers(dest="action", required=True)
    cm0_sub.add_parser("status", help="Power zones, RUN line and USB mode (read-only)")
    cm0_sub.add_parser("on", help="Power FPGA and CM0 (zones 6, 17 + RUN) and wait for Linux")
    cm0_sub.add_parser("reboot", help="Clean Linux reboot through the shell, then wait")
    reset = cm0_sub.add_parser("reset", help="Hard reset via the RUN line when the shell is stuck")
    reset.add_argument("--force", action="store_true",
                       help="confirm resetting without a Linux shutdown")
    cycle = cm0_sub.add_parser("power-cycle", help="Switch CM0 power (zone 17) off and on")
    cycle.add_argument("--force", action="store_true",
                       help="confirm cutting power without a Linux shutdown")
    usb = cm0_sub.add_parser("usb", help="Query or switch CM0 USB at runtime")
    usb.add_argument("mode", choices=("status", "host", "gadget"))
    args = parser.parse_args(argv)

    try:
        if args.command == "ports":
            ports = fwlink.find_main_ports()
            print("\n".join(ports) if ports else "No FreeWili 2 MAIN port found.")
            return 0 if ports else 1
        resolved = []

        def port() -> str:  # resolved on first use, so injected factories need no device
            if not resolved:
                resolved.append(fwlink.resolve_port(args.port))
            return resolved[0]
        shell_factory = shell_factory or (lambda: fwlink.LinuxShell(port()))
        menu_factory = menu_factory or (lambda: fwlink.MenuPort(port()))
        if args.command == "cm0":
            return cmd_cm0(args, menu_factory, shell_factory)
        if args.command == "install":
            return cmd_install(args, shell_factory)
        with shell_factory() as shell:
            if args.command == "info":
                _, out = shell.run("tr -d '\\0' < /proc/device-tree/model; echo; uname -srm; "
                                   "python3 --version; command -v fwcm0 || echo 'fwcm0: missing'; "
                                   f"echo 'apps:'; ls -1 {shlex.quote(APPS_DIR)} 2>/dev/null || echo '  ({APPS_DIR} missing)'")
                print(out)
            elif args.command == "run":
                name = fw.app_name(args.app)
                print(start_app(shell, name))
                time.sleep(3)
                print(show_log(shell, name, 40))
            elif args.command == "stop":
                name = fw.app_name(args.app)
                print(f"Stopped {name}." if stop_app(shell, name) else f"{name} is not running.")
            elif args.command == "log":
                print(show_log(shell, fw.app_name(args.app) if args.app else None, args.lines))
            elif args.command == "shell":
                code, out = shell.run(" ".join(args.words), timeout=300)
                print(out)
                return code
        return 0
    except (fwlink.LinkError, TimeoutError, RuntimeError, argparse.ArgumentTypeError) as error:
        print(f"deploy: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
