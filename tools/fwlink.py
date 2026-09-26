"""PC-side link to CM0 Linux through the FreeWili 2 MAIN USB serial port.

This uses the MAIN framed-menu shell commands (the same path FreeWili GUI's
Linux Console uses): ``l\\c`` opens a CM0 shell session, ``l\\w``/``l\\r``
write and read it in 192-byte pieces, and ``l\\e`` closes it. It needs no
network, SSH, or CM0 Gadget Serial -- only the USB cable to MAIN and pyserial.

Only one program can own the MAIN serial port. Disconnect FreeWili GUI (or
close its Linux Console) before using these tools.
"""
from __future__ import annotations

import base64
import hashlib
import re
import secrets
import shlex
import time

MAIN_VID = 0x093C
MAIN_PIDS = (0x205A, 0x2060)  # FreeWili 2 MAIN application CDC
BAUD = 1_000_000
SHELL_CHUNK = 192     # bytes per l\w / l\r request, fixed by MAIN firmware
UPLOAD_CHUNK = 2800   # base64 chars per shell line; stays under the 4 KiB tty line limit
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]")  # colour / paste-mode noise


class LinkError(RuntimeError):
    pass


def find_main_ports() -> list[str]:
    """Serial devices that look like a FreeWili 2 MAIN USB port."""
    from serial.tools import list_ports
    return [p.device for p in list_ports.comports()
            if p.vid == MAIN_VID and p.pid in MAIN_PIDS]


def resolve_port(port: str | None) -> str:
    if port:
        return port
    ports = find_main_ports()
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise LinkError("No FreeWili 2 MAIN USB port found. Connect USB to MAIN, "
                        "or pass --port (for example COM183 or /dev/ttyACM0).")
    raise LinkError("Several FreeWili 2 devices found (%s); pass --port." % ", ".join(ports))


def parse_reply(line: bytes, tag: str) -> str | None:
    """Return a reply body for ``tag`` from one framed-menu line, or None.

    MAIN replies look like ``[tag <a> <b> <body> 1]`` (or ``[\\tag ...]``); the
    trailing field is 1 for success. A failed reply raises LinkError.
    """
    for prefix in (b"[" + tag.encode() + b" ", b"[\\" + tag.encode() + b" "):
        start = line.find(prefix)
        if start >= 0:
            break
    else:
        return None
    fields = line[start + 1:].rstrip(b"\r").split(b" ", 3)
    if len(fields) != 4 or fields[0].lstrip(b"\\").decode(errors="replace") != tag:
        return None
    body = fields[3]
    if not body.endswith(b" 1]"):
        raise LinkError(f"{tag}: {body.decode(errors='replace')}")
    return body[:-3].decode("ascii", errors="replace")


class MenuPort:
    """Framed (non-interactive) MAIN menu over USB serial."""

    def __init__(self, port: str, serial_factory=None):
        if serial_factory is None:
            import serial
            serial_factory = serial.Serial
        try:
            self.port = serial_factory(port, BAUD, timeout=0.05, write_timeout=2)
        except Exception as error:  # SerialException / PermissionError
            raise LinkError(f"Cannot open {port}: {error}. Is FreeWili GUI still "
                            "connected? Disconnect it and try again.") from error
        self.port.write(b"\x02")  # framed, non-echoing menu mode
        self.port.reset_input_buffer()

    def call(self, command: str, timeout: float = 5.0) -> str:
        tag = command.split(" ", 1)[0]
        self.port.write((command + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        incoming = b""
        while time.monotonic() < deadline:
            incoming += self.port.read(max(1, self.port.in_waiting))
            while b"\n" in incoming:
                line, incoming = incoming.split(b"\n", 1)
                body = parse_reply(line, tag)
                if body is not None:
                    return body
        # Never replay: a lost reply may belong to a write that already happened.
        raise TimeoutError(f"{tag}: no reply from MAIN")

    def wait_ready(self, timeout: float = 60.0) -> None:
        """Retry the read-only identify query while MAIN finishes booting."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.call("?")
                return
            except TimeoutError:
                if time.monotonic() >= deadline:
                    raise LinkError("MAIN did not answer on this port")

    def close(self) -> None:
        self.port.close()


class LinuxShell:
    """A CM0 Linux shell session tunnelled through MAIN.

    Use as a context manager. ``run()`` executes one shell command and returns
    ``(exit_code, output)``; ``upload()`` copies bytes to a CM0 path.
    """

    def __init__(self, port: str, serial_factory=None):
        self.menu = MenuPort(port, serial_factory)
        self.token = f"{secrets.randbits(32) or 1:08X}"
        self.opened = False
        self.running = True

    # -- session -------------------------------------------------------------
    def open(self, timeout: float = 20.0, busy_wait: float = 10.0) -> "LinuxShell":
        try:
            self.menu.wait_ready()
            # Count the session as ours before the reply: if the reply is lost, close()
            # still releases it (only this token can close it, so this is harmless).
            busy_until = time.monotonic() + busy_wait
            while True:
                self.opened = True
                try:
                    opened = self.menu.call(f"l\\c {self.token}")
                    break
                except LinkError as error:
                    self.opened = False  # refused, so not ours
                    # A just-closed session's detach is still queued for CM0 for a
                    # moment; a refused open changed nothing, so retrying is safe.
                    if "EBUSY" in str(error) and time.monotonic() < busy_until:
                        time.sleep(0.25)
                        continue
                    busy_error = error
                    break
            if not self.opened:
                error = busy_error
                if "EBUSY" in str(error):
                    raise LinkError(
                        "MAIN reports the Linux shell busy (EBUSY). If CM0 just booted, wait "
                        "a minute and retry. Otherwise close the Linux Terminal on the FreeWili "
                        "screen or FreeWili GUI's Linux Console. MAIN firmware older than the "
                        "linkReset fix keeps an orphaned session until the whole FreeWili is "
                        "power-cycled.") from error
                raise error
            if opened != self.token:
                raise LinkError("MAIN refused the Linux shell session (is CM0 powered and booted?)")
            # Quiet, prompt-free shell so command output is exactly what we read.
            marker = "FWREADY" + secrets.token_hex(4)
            deadline, output = time.monotonic() + timeout, b""
            while marker.encode() not in [l.strip() for l in output.splitlines()]:
                if time.monotonic() > deadline:
                    raise LinkError("CM0 Linux shell did not respond. Enable Linux/CM0 power "
                                    "and wait for Linux to finish booting.")
                self.write(f"\rstty -echo; export TERM=dumb; PS1=''; PS2=''; "
                           f"bind 'set enable-bracketed-paste off' 2>/dev/null; "
                           f"printf '\\n{marker}\\n'\r".encode())
                for _ in range(10):
                    output += self.read()
                    time.sleep(0.03)
            # A slow, just-booted shell may still echo repeated setup lines; drain
            # them so they do not appear in the first command's output.
            quiet_until, give_up = time.monotonic() + 0.5, time.monotonic() + 3.0
            while time.monotonic() < min(quiet_until, give_up):
                if self.read():
                    quiet_until = time.monotonic() + 0.5
                time.sleep(0.03)
            return self
        except BaseException:
            self.close()
            raise

    def close(self) -> None:
        try:
            if self.opened:
                self.menu.call(f"l\\e {self.token}")
        except Exception:
            pass
        finally:
            self.opened = False
            self.menu.close()

    def __enter__(self) -> "LinuxShell":
        return self.open()

    def __exit__(self, *exc) -> None:
        self.close()

    # -- raw I/O -------------------------------------------------------------
    def write(self, data: bytes, timeout: float = 20.0) -> None:
        deadline = time.monotonic() + timeout
        while data:
            if time.monotonic() >= deadline:
                raise LinkError("CM0 shell input did not drain")
            chunk = data[:SHELL_CHUNK]
            count = int(self.menu.call(f"l\\w {self.token} {chunk.hex()}"))
            if not 0 <= count <= len(chunk):
                raise LinkError("Invalid shell write count")
            data = data[count:]
            if not count:
                time.sleep(0.01)

    def read(self) -> bytes:
        count, data, running = self.menu.call(f"l\\r {self.token} {SHELL_CHUNK}").split()
        value = bytes.fromhex(data) if int(count) else b""
        if len(value) != int(count) or running not in ("0", "1"):
            raise LinkError("Invalid shell reply")
        self.running = running == "1"
        return value

    # -- commands ------------------------------------------------------------
    def run(self, command: str, timeout: float = 60.0, check: bool = False) -> tuple[int, str]:
        """Run one shell command; return (exit code, combined output)."""
        marker = "FWDONE" + secrets.token_hex(4)
        # "cmd &; printf" is a syntax error; a trailing & already separates commands.
        joiner = " " if command.rstrip().endswith("&") else "; "
        self.write((command + f"{joiner}printf '\\n{marker}:%d\\n' $?\r").encode())
        deadline, output = time.monotonic() + timeout, b""
        while True:
            piece = self.read()
            if not piece and not self.running:
                raise LinkError("CM0 shell session ended")
            output += piece
            text = ANSI.sub("", output.decode(errors="replace")).replace("\r\n", "\n").replace("\r", "\n")
            lines = text.split("\n")
            for index, line in enumerate(lines):
                if line.strip().startswith(marker + ":"):
                    code = int(line.strip().split(":", 1)[1] or 1)
                    body = "\n".join(lines[:index]).strip("\n")
                    if check and code:
                        raise LinkError(f"`{command[:80]}` failed ({code}):\n{body}")
                    return code, body
            if time.monotonic() > deadline:
                raise TimeoutError(f"`{command[:80]}` did not finish:\n{text[-2000:]}")
            time.sleep(0.005)

    def python(self, source: str, **kwargs) -> tuple[int, str]:
        return self.run("python3 -c " + shlex.quote(source), **kwargs)

    def upload(self, data: bytes, remote: str, progress=None) -> None:
        """Copy ``data`` to the CM0 file ``remote`` and verify its SHA-256."""
        encoded = base64.b64encode(data).decode("ascii")
        staging = remote + ".b64"
        self.run(f"rm -f {shlex.quote(staging)}", check=True)
        total = len(encoded)
        for start in range(0, total, UPLOAD_CHUNK):
            piece = encoded[start:start + UPLOAD_CHUNK]
            self.run(f"printf %s {piece} >> {shlex.quote(staging)}", check=True)
            if progress:
                progress(min(start + UPLOAD_CHUNK, total), total)
        digest = hashlib.sha256(data).hexdigest()
        self.python(
            "import base64,hashlib,pathlib;"
            f"s=pathlib.Path({staging!r});d=base64.b64decode(s.read_bytes());"
            f"assert hashlib.sha256(d).hexdigest()=={digest!r},'checksum mismatch';"
            f"pathlib.Path({remote!r}).write_bytes(d);s.unlink()",
            check=True)
