"""Linux integration tests: script handling, actual entry points and bad DCFs."""

from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / "linux"))
from bluetrum_cli import parse_xm, run_xm
from bluetrum_dcf import parse_dcf
from xmaker_re import build_dcf, xmaker_crc16


class LinuxCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (TOOLS / "header.bin").read_bytes()
        cls.app = bytes(range(256)) * 2 + b"boundary"
        cls.dcf = build_dcf(cls.header, cls.app)

    def test_default_container_and_corruption(self):
        image = parse_dcf(self.dcf)
        self.assertEqual((image.chip, image.version), ("CRWN", 1))
        self.assertEqual(len(image.segments), 1)
        for offset in (0, 4, 12, 24, 36, 48, 56, 64, 72, 76, 78, 84, 88, len(self.dcf) - 1):
            damaged = bytearray(self.dcf)
            damaged[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                parse_dcf(bytes(damaged))
        for length in (0, 4, 8, 16, 79, 87, 88, len(self.dcf) - 1):
            with self.subTest(length=length), self.assertRaises(ValueError):
                parse_dcf(self.dcf[:length])

    def test_valid_crc_does_not_allow_invalid_segment(self):
        data = bytearray(self.dcf)
        struct.pack_into("<I", data, 64, len(data) + 1)
        struct.pack_into("<H", data, 78, xmaker_crc16(data[:78]))
        with self.assertRaisesRegex(ValueError, "segment outside"):
            parse_dcf(bytes(data))
        data = bytearray(self.dcf)
        struct.pack_into("<I", data, 60, 512)
        struct.pack_into("<H", data, 78, xmaker_crc16(data[:78]))
        with self.assertRaisesRegex(ValueError, "XCOD layout"):
            parse_dcf(bytes(data))

    def test_xm_both_encodings_and_paths_with_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "header.bin").write_bytes(self.header)
            (root / "my app.bin").write_bytes(self.app)
            for encoding in ("utf-8-sig", "utf-16"):
                script = root / "test.xm"
                script.write_bytes('make(buf, header.bin, "my app.bin");\nsave(buf, "out image.dcf");'.encode(encoding))
                self.assertEqual(run_xm(script), [root / "out image.dcf"])
                self.assertEqual((root / "out image.dcf").read_bytes(), self.dcf)

    def test_reject_script_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "test.xm"
            (root / "header.bin").write_bytes(self.header)
            (root / "app.bin").write_bytes(self.app)
            for tail in ("download", "resource(x);", "save(undefined, out2.dcf);", "save(buf, app.bin);"):
                script.write_text("make(buf, header.bin, app.bin);save(buf, out.dcf);" + tail)
                with self.subTest(tail=tail), self.assertRaises(ValueError):
                    run_xm(script)
                self.assertFalse((root / "out.dcf").exists())
                self.assertEqual((root / "app.bin").read_bytes(), self.app)
            for raw in (b"", b"make(x,a,b,);", b"make(x,a);", b"save(x y,a);", b"make(x,a,b)"):
                with self.subTest(raw=raw), self.assertRaises(ValueError):
                    parse_xm(raw)

    def test_executable_entry_points(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "app.bin"
            output = root / "result.dcf"
            app.write_bytes(self.app)
            result = subprocess.run([str(TOOLS / "linux/convert"), str(TOOLS / "header.bin"),
                                     str(app), str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), self.dcf)
            script = root / "build.xm"
            script.write_text(f'make(buf, "{TOOLS / "header.bin"}", app.bin);save(buf, script.dcf);')
            result = subprocess.run([str(TOOLS / "linux/riscv32-elf-xmaker"), "-b", str(script)],
                                    cwd="/tmp", capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "script.dcf").read_bytes(), self.dcf)
            result = subprocess.run([str(TOOLS / "linux/convert"), str(TOOLS / "header.bin"),
                                     str(app), str(app)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(app.read_bytes(), self.app)

    def test_download_plan_does_not_open_serial(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.dcf"
            path.write_bytes(self.dcf)
            result = subprocess.run([str(TOOLS / "linux/downloader"), "flash", str(path),
                                     "--dry-run", "--port", "/does/not/exist"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["chip"], "CRWN")
            self.assertFalse(plan["serial_opened"])
            self.assertEqual(plan["loader_bytes"], 9408)
            path.write_bytes(self.dcf[:-1])
            result = subprocess.run([str(TOOLS / "linux/downloader"), "flash", str(path),
                                     "--port", "/does/not/exist"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("DCF file length mismatch", result.stderr)
            self.assertNotIn("Connecting", result.stderr)

    def test_invalid_serial_device_reports_error_without_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not-a-tty"
            path.write_bytes(b"unchanged")
            result = subprocess.run([str(TOOLS / "linux/downloader"), "probe", "--port", str(path)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("error:", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(path.read_bytes(), b"unchanged")


if __name__ == "__main__":
    unittest.main()
