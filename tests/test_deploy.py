"""PC deploy tools against a simulated MAIN framed-menu shell (no FreeWili).

FakeMain speaks the MAIN serial protocol (``?``, ``l\\c``, ``l\\w``, ``l\\r``,
``l\\e``) and executes the few tunnelled shell commands the tools use on this
host, so framing, chunking, upload checksums, and the on-CM0 extract script
run for real. It is not hardware verification.
"""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import fwlink  # noqa: E402

spec = importlib.util.spec_from_file_location("deploy", TOOLS / "deploy.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

DONE = re.compile(r"^(.*?);? printf '\\n(FWDONE[0-9a-f]+):%d\\n' \$\?$", re.S)
READY = re.compile(r"printf '\\n(FWREADY[0-9a-f]+)\\n'$")


class FakeMain:
    """Serial double for MAIN with a tiny CM0 shell behind l\\w / l\\r."""

    def __init__(self, home):
        self.home = home
        self.rx = b""
        self.to_host = b""
        self.shell_in = b""
        self.shell_out = b""
        self.writes = []      # sizes of each l\w payload, to check chunking
        self.commands = []
        self.in_waiting = 0

    # pyserial surface used by fwlink.MenuPort
    def reset_input_buffer(self):
        self.to_host = b""

    def close(self):
        pass

    def read(self, size):
        data, self.to_host = self.to_host[:size], self.to_host[size:]
        return data

    def write(self, data):
        if data in (b"\x02", b"\x03"):
            return
        self.rx += data
        while b"\n" in self.rx:
            line, self.rx = self.rx.split(b"\n", 1)
            self.menu(line.decode())

    def reply(self, tag, body):
        self.to_host += f"noise\n[{tag} 0 0 {body} 1]\r\n".encode()

    def menu(self, line):
        tag, _, rest = line.partition(" ")
        args = rest.split()
        if tag == "?":
            self.reply(tag, "FreeWili2")
        elif tag == "l\\c":
            self.reply(tag, args[0])
        elif tag == "l\\w":
            data = bytes.fromhex(args[1])
            self.writes.append(len(data))
            self.shell_in += data
            self.execute()
            self.reply(tag, str(len(data)))
        elif tag == "l\\r":
            chunk = self.shell_out[:int(args[1])]
            self.shell_out = self.shell_out[len(chunk):]
            self.reply(tag, f"{len(chunk)} {chunk.hex() or '-'} 1")
        elif tag == "l\\e":
            self.reply(tag, "")

    def execute(self):
        while b"\r" in self.shell_in:
            line, self.shell_in = self.shell_in.split(b"\r", 1)
            text = line.decode()
            if not text.strip():
                continue
            ready = READY.search(text)
            if ready:
                self.shell_out += f"\r\n{ready.group(1)}\r\n".encode()
                continue
            match = DONE.match(text)
            command, marker = match.group(1), match.group(2)
            self.commands.append(command)
            code, output = self.run(command)
            self.shell_out += (output.replace("\n", "\r\n") + f"\r\n{marker}:{code}\r\n").encode()

    def run(self, command):
        words = shlex.split(command)
        if words[:2] == ["rm", "-f"]:
            Path(words[2]).unlink(missing_ok=True)
            return 0, ""
        if words[:2] == ["printf", "%s"] and words[3] == ">>":
            with open(words[4], "a", encoding="ascii") as handle:
                handle.write(words[2])
            return 0, ""
        if words[:2] == ["python3", "-c"]:
            env = dict(os.environ, HOME=str(self.home), USERPROFILE=str(self.home))
            done = subprocess.run([sys.executable, "-c", words[2]], capture_output=True,
                                  text=True, env=env)
            return done.returncode, (done.stdout + done.stderr).strip()
        if words[0] == "test" and words[1] in ("-d", "-w"):
            path = Path(words[2])
            ok = path.is_dir() if words[1] == "-d" else os.access(path, os.W_OK)
            return (0 if ok else 1), ""
        if words == ["colour"]:
            return 0, "\x1b[?2004l\x1b[01;34mblue\x1b[0m"
        if command.rstrip().endswith("&"):  # backgrounded: the shell returns at once
            return 0, ""
        if words[0] == "pgrep":
            return 1, ""
        return 127, f"fake shell: unsupported {command}"


class ReplyParsing(unittest.TestCase):
    def test_success_failure_and_unrelated_lines(self):
        self.assertEqual(fwlink.parse_reply(b"[l\\r 0 0 2 abcd 1 1]\r", "l\\r"), "2 abcd 1")
        self.assertEqual(fwlink.parse_reply(b"junk [\\l\\c 1 2 TOKEN 1]", "l\\c"), "TOKEN")
        self.assertIsNone(fwlink.parse_reply(b"[l\\w 0 0 5 1]", "l\\r"))
        self.assertIsNone(fwlink.parse_reply(b"plain text", "?"))
        with self.assertRaises(fwlink.LinkError):
            fwlink.parse_reply(b"[l\\c 0 0 busy 0]", "l\\c")


class ShellTunnel(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.fake = FakeMain(self.root / "home")
        (self.root / "home").mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def shell(self):
        return fwlink.LinuxShell("FAKE", serial_factory=lambda *a, **k: self.fake)

    def test_run_returns_exit_code_and_clean_output(self):
        with self.shell() as shell:
            code, out = shell.python("print('hello from cm0')")
            self.assertEqual((code, out), (0, "hello from cm0"))
            self.assertEqual(shell.run("colour"), (0, "blue"))  # terminal noise stripped
            self.assertEqual(shell.run("sleep 1 &"), (0, ""))   # no "&;" syntax error
            self.assertTrue(self.fake.commands[-1].endswith("sleep 1 &"))
            code, _ = shell.python("raise SystemExit(3)")
            self.assertEqual(code, 3)
            with self.assertRaises(fwlink.LinkError):
                shell.run("definitely-not-a-command", check=True)
        self.assertTrue(all(size <= fwlink.SHELL_CHUNK for size in self.fake.writes))

    def test_upload_is_exact_and_verified(self):
        data = bytes(range(256)) * 40 + b"\x00\xff'\"$`\\\n\r]"
        target = self.root / "up load.bin"
        seen = []
        with self.shell() as shell:
            shell.upload(data, str(target), lambda done, total: seen.append((done, total)))
        self.assertEqual(target.read_bytes(), data)
        self.assertFalse(Path(str(target) + ".b64").exists())
        self.assertEqual(seen[-1][0], seen[-1][1])
        self.assertGreater(len(seen), 1)  # really chunked


class InstallOverTunnel(unittest.TestCase):
    def test_install_publishes_self_contained_app_and_guards_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            source = temp / "bsp"
            (source / "apps/hello").mkdir(parents=True)
            (source / "apps/hello/app.py").write_text("print('hello')\n")
            runtime = source / ".runtime"
            runtime.mkdir()
            (runtime / "onewili_cm0.py").write_text("# fixture\n")
            apps = temp / "cm0 apps"
            apps.mkdir()
            (temp / "home").mkdir()
            fake = FakeMain(temp / "home")

            def factory():
                return fwlink.LinuxShell("FAKE", serial_factory=lambda *a, **k: fake)

            with patch.object(deploy.fw, "ROOT", source), patch.object(deploy.fw, "RUNTIME", runtime), \
                    patch.object(deploy, "APPS_DIR", str(apps)):
                self.assertEqual(deploy.main(["install", "hello"], shell_factory=factory), 0)
                installed = apps / "hello"
                self.assertEqual((installed / "app.py").read_text(), "print('hello')\n")
                self.assertTrue((installed / ".lib/onewili_cm0.py").is_file())
                self.assertNotIn(b"\r", (installed / "run.sh").read_bytes())

                (installed / "app.py").write_text("print('edited on device')\n")
                self.assertNotEqual(deploy.main(["install", "hello"], shell_factory=factory), 0)
                self.assertEqual((installed / "app.py").read_text(), "print('edited on device')\n")

                self.assertEqual(deploy.main(["install", "hello", "--replace"], shell_factory=factory), 0)
                self.assertEqual((installed / "app.py").read_text(), "print('hello')\n")
                backups = list((temp / "home/.local/share/fw-deploy/backups").iterdir())
                self.assertEqual(len(backups), 1)
                self.assertIn("edited on device", (backups[0] / "app.py").read_text())
            # Staging folders and uploaded zips are cleaned up on the "CM0".
            self.assertEqual(sorted(p.name for p in apps.iterdir()), ["hello"])

    def test_cm0_commands_use_main_menu_and_guard_reset(self):
        class FakeMenu:
            calls = []

            def wait_ready(self):
                pass

            def call(self, command, timeout=5.0):
                self.calls.append(command)
                if command.startswith("l\\u"):
                    raise TimeoutError("l\\u: no reply from MAIN")  # older CM0 image
                return "ok"

            def close(self):
                pass

        def run(*argv):
            FakeMenu.calls = []
            with patch.object(deploy, "wait_for_linux", return_value=True), \
                    patch.object(deploy.time, "sleep"):
                code = deploy.main(["cm0", *argv], shell_factory=lambda: None,
                                   menu_factory=FakeMenu)
            return code, FakeMenu.calls

        self.assertEqual(run("status"), (0, ["h\\p\\g", "h\\p\\n", "l\\u status"]))
        self.assertEqual(run("reset"), (1, []))  # refuses without --force
        self.assertEqual(run("reset", "--force"), (0, ["h\\p\\c 0", "h\\p\\c 1"]))
        self.assertEqual(run("usb", "host")[0], 1)  # no reply is reported, not retried
        self.assertEqual(FakeMenu.calls, ["l\\u host"])

    def test_bad_app_names_never_reach_the_device(self):
        called = []
        code = deploy.main(["install", "../escape"], shell_factory=lambda: called.append(1))
        self.assertEqual((code, called), (1, []))


if __name__ == "__main__":
    unittest.main()
