import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("fw", Path(__file__).resolve().parents[1] / "tools/fw.py")
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)


class AppTools(unittest.TestCase):
    def test_names_cannot_escape_apps_directory(self):
        for value in ("../escape", "/tmp/app", "name;rm", "with space", "", "A", "x" * 41):
            with self.assertRaises(argparse.ArgumentTypeError):
                fw.app_name(value)
        self.assertEqual(fw.app_name("scope_2"), "scope_2")

    def test_python_install_is_self_contained_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "source with spaces"
            app = root / "apps/hello"
            app.mkdir(parents=True)
            (app / "app.py").write_text("print('hello')\n")
            runtime = root / ".runtime"
            runtime.mkdir()
            (runtime / "onewili_cm0.py").write_text("# fixture\n")
            with patch.object(fw, "ROOT", root), patch.object(fw, "RUNTIME", runtime):
                destination = Path(temp) / "apps"
                fw.install_app("hello", destination, root / "build")
                installed = destination / "hello"
                self.assertTrue((installed / ".lib/onewili_cm0.py").is_file())
                script = (installed / "run.sh").read_bytes()
                self.assertNotIn(b"\r", script)
                self.assertIn(b'"$APP_DIR/app.py"', script)
                self.assertNotIn(str(root).encode(), script)
                with self.assertRaises(RuntimeError):
                    fw.install_app("hello", destination, root / "build")
                self.assertEqual((installed / "app.py").read_text(), "print('hello')\n")

    def test_native_install_and_missing_binary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = root / "build/apps/hello/hello"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"test executable")
            with patch.object(fw, "ROOT", root):
                fw.install_app("hello", root / "apps-out", root / "build")
                self.assertEqual((root / "apps-out/hello/hello").read_bytes(), binary.read_bytes())
                with self.assertRaises(RuntimeError):
                    fw.install_app("missing", root / "apps-out", root / "build")
                self.assertFalse((root / "apps-out/missing").exists())


if __name__ == "__main__":
    unittest.main()
