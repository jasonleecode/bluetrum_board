"""Regression hashes captured from the original-code oracle; no Unicorn needed."""
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xmaker_re as x


class DcfCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[2]
        cls.header = (cls.root / 'tools/header.bin').read_bytes()
        cls.evidence = json.loads(Path(__file__).with_name('xmaker_oracle_evidence.json').read_text())

    def test_original_code_golden_hashes(self):
        self.assertEqual(hashlib.sha256(self.header).hexdigest(), self.evidence['header_sha256'])
        for row in self.evidence['cases']:
            with self.subTest(app_size=row['app_size']):
                app = bytes(((i * 73 + (i >> 8) * 19 + 29) & 255)
                            for i in range(row['app_size']))
                self.assertEqual(hashlib.sha256(app).hexdigest(), row['app_sha256'])
                result = x.build_dcf(self.header, app)
                self.assertEqual(len(result), row['dcf_size'])
                self.assertEqual(hashlib.sha256(result).hexdigest(), row['dcf_sha256'])

    def test_rejects_unverified_inputs(self):
        for header, app in ((self.header[:-1], b'a'), (bytes(4096), b'a'),
                            (self.header, b''), (self.header, bytes(x.MAX_APP_SIZE + 1))):
            with self.subTest(header_size=len(header), app_size=len(app)):
                with self.assertRaises(ValueError):
                    x.build_dcf(header, app)

    def test_tables_match_original_executable(self):
        exe = (self.root / 'tools/windows/riscv32-elf-xmaker.exe').read_bytes()
        self.assertEqual(hashlib.sha256(exe).hexdigest(), self.evidence['vendor_exe_sha256'])
        self.assertEqual(x.make_xor32_table(), x.extract_xor32_table(exe))
        self.assertEqual(x.make_crc16_table(), x.extract_crc16_table(exe))


if __name__ == '__main__':
    unittest.main()
