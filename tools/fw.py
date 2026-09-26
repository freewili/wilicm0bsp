#!/usr/bin/env python3
"""Portable setup, scaffolding and installation for FreeWili 2 Linux apps."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / ".runtime"


def app_name(value):
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,39}", value):
        raise argparse.ArgumentTypeError("Use a lowercase name with letters, digits and underscores (max 40).")
    return value


def setup():
    source = ROOT / "libs/onewili"
    if not (source / "cm0/python/onewili_cm0.py").is_file():
        raise RuntimeError("Run git submodule update --init --recursive first.")
    RUNTIME.mkdir(exist_ok=True)
    # CM0 uses the mailbox, so the USB discovery/serial dependencies are unnecessary.
    subprocess.run([sys.executable, "-m", "pip", "install", "--upgrade", "--no-compile",
                    "--target", str(RUNTIME), "result==0.17.0"], check=True)
    shutil.copytree(source / "python/onewili", RUNTIME / "onewili", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copy2(source / "cm0/python/onewili_cm0.py", RUNTIME / "onewili_cm0.py")
    shutil.copy2(source / "LICENSE", RUNTIME / "ONEWILI-LICENSE")
    print("Python runtime ready. Run: python3 tools/fw.py run hello_python")


def new_app(name):
    source = ROOT / "apps/template"
    destination = ROOT / "apps" / app_name(name)
    shutil.copytree(source, destination)  # refuses an existing app
    (destination / "AGENTS.md").write_text(
        "# Application instructions\n\nRead [the BSP agent guide](../../AGENTS.md) before editing.\n"
        "Install in `/home/apps/" + name + "/` for Linux Apps launching.\n", encoding="utf-8")
    print(destination)


def run_app(name):
    source = ROOT / "apps" / app_name(name) / "app.py"
    if not (RUNTIME / "onewili_cm0.py").is_file():
        raise RuntimeError("Run python3 tools/fw.py setup first.")
    env = dict(os.environ, PYTHONPATH=str(RUNTIME) + os.pathsep + os.environ.get("PYTHONPATH", ""))
    subprocess.run([sys.executable, "-u", str(source)], cwd=source.parent, env=env, check=True)


def install_app(name, destination, build):
    name = app_name(name)
    destination = Path(destination).resolve()
    target = destination / name
    if target.exists() or target.is_symlink():
        raise RuntimeError(f"{target} already exists; choose another name/folder or remove the old app yourself.")
    source = ROOT / "apps" / name
    python_app = (source / "app.py").is_file()
    binary = Path(build).resolve() / "apps" / name / name
    if python_app and not (RUNTIME / "onewili_cm0.py").is_file():
        raise RuntimeError("Run python3 tools/fw.py setup first.")
    if not python_app and not binary.is_file():
        raise RuntimeError(f"No Python app or compiled Linux executable found for {name}: {binary}")
    destination.mkdir(parents=True, exist_ok=True)
    # Stage before publishing. Existing installations are never merged or replaced.
    with tempfile.TemporaryDirectory(prefix=".install-", dir=destination) as temporary:
        staged = Path(temporary) / name
        staged.mkdir()
        if python_app:
            shutil.copytree(source, staged, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "AGENTS.md"))
            shutil.copytree(RUNTIME, staged / ".lib",
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            launch = 'export PYTHONPATH="$APP_DIR/.lib${PYTHONPATH:+:$PYTHONPATH}"\nexec python3 -u "$APP_DIR/app.py" "$@"\n'
        else:
            shutil.copy2(binary, staged / name)
            (staged / name).chmod(0o755)
            launch = 'exec "$APP_DIR/' + name + '" "$@"\n'
        script = staged / "run.sh"
        script.write_text('#!/bin/sh\nset -eu\nAPP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\ncd "$APP_DIR"\n' + launch,
                          encoding="utf-8", newline="\n")
        script.chmod(0o755)
        for notice in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
            if (ROOT / notice).is_file():
                shutil.copy2(ROOT / notice, staged / notice)
        # Reserve the final directory exclusively; copy completes synchronously.
        target.mkdir()
        shutil.copytree(staged, target, dirs_exist_ok=True)
    print(f"Installed {target}/run.sh. Open Linux > Apps, enter {name}, and launch run.sh.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("setup", help="Prepare a local, relocatable Python runtime")
    new = sub.add_parser("new-app", help="Create a Python application from the template")
    new.add_argument("name", type=app_name)
    run = sub.add_parser("run", help="Run a Python app on the CM0")
    run.add_argument("name", type=app_name)
    install = sub.add_parser("install", help="Install an app into the Linux Apps folder")
    install.add_argument("name", type=app_name)
    install.add_argument("--apps-dir", default="/home/apps", help="Linux launcher folder, or a local staging folder")
    install.add_argument("--build-dir", default=str(ROOT / "build"))
    args = parser.parse_args()
    try:
        if args.command == "setup": setup()
        elif args.command == "new-app": new_app(args.name)
        elif args.command == "run": run_app(args.name)
        else: install_app(args.name, args.apps_dir, args.build_dir)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
