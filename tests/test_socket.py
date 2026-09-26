"""A real local socket peer exercises the C++ transport without a FreeWili."""
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

PROBE = sys.argv.pop(1)


class BridgeSocket(unittest.TestCase):
    def check_peer(self, mode):
        errors = []
        with tempfile.TemporaryDirectory(prefix="ow-", dir="/tmp") as temp:
            path = str(Path(temp) / "bridge.sock")
            server = socket.socket(socket.AF_UNIX)
            server.bind(path)
            server.listen()
            server.settimeout(10)

            def serve():
                try:
                    with server.accept()[0] as client:
                        client.settimeout(8)
                        header = b""
                        while len(header) < 5:
                            header += client.recv(5 - len(header))
                        self.assertEqual(header, b"\x01\0\0\0\x07")
                        if mode == "busy":
                            client.sendall(b"fwcm0: console busy (another session is active)\r\n")
                            return
                        if mode == "disconnect": return
                        incoming = b""
                        count = 0
                        while data := client.recv(4096):
                            incoming += data
                            while b"\n" in incoming:
                                line, incoming = incoming.split(b"\n", 1)
                                command = line.lstrip(b"\x02").strip()
                                if not command: continue
                                count += 1
                                if count == 1:
                                    self.assertEqual(command, b"h\\a\\g")
                                    response = b"[h\\a\\g 1 1 main 1 0 150000000 none 1]\n"
                                else:
                                    self.assertEqual(command, b"i\\g\\u")
                                    response = b"[i\\g\\u 2 2 12345678 1]\n"
                                for byte in response:
                                    client.sendall(bytes([byte]))
                                    time.sleep(0.0002)
                        self.assertEqual(count, 2)
                except BaseException as error: errors.append(error)

            thread = threading.Thread(target=serve)
            thread.start()
            try:
                result = subprocess.run([PROBE, path], capture_output=True, text=True, timeout=12)
            finally:
                thread.join(timeout=10)
                server.close()
            self.assertFalse(thread.is_alive())
            if errors: raise errors[0]
            return result

    def test_fragmented_request_response_and_graceful_release(self):
        for _ in range(3):
            result = self.check_peer("success")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("GPIO=12345678", result.stdout)

    def test_busy_and_disconnect_fail_promptly(self):
        for mode in ("busy", "disconnect"):
            result = self.check_peer(mode)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("connection probe failed", result.stderr)

    def test_missing_socket(self):
        result = subprocess.run([PROBE, "/does-not-exist/wilicm0.sock"], capture_output=True, text=True, timeout=4)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Cannot connect", result.stderr)


if __name__ == "__main__":
    unittest.main()
