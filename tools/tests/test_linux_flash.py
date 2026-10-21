"""Check failure boundaries and commit order against recovered downloader rules."""

from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux"))
from bluetrum_dcf import parse_dcf
from bluetrum_flash import flash_image, plan_flash, VerificationError
from bluetrum_uart import RequestContext, crc16
from xmaker_re import build_dcf


class FakeFlash:
    """Models Flash erase/program plus loader CRC replies; has no serial access."""
    def __init__(self, size=1 << 20, corrupt_crc=False):
        self.info = SimpleNamespace(chip="CRWN")
        self.context = RequestContext(operation_mask=14)
        self.memory = bytearray(b"\x55" * size)
        self.commands = []
        self.corrupt_crc = corrupt_crc

    def ping(self):
        self.commands.append(("ping",))
        return True

    def refresh_key(self):
        self.commands.append(("key",))

    def request(self, index, *, value=0, selector=0, count=0, read_size=0, write_data=b""):
        self.commands.append((index, value, selector, count))
        if index == 28:
            length = {1: 65536, 3: 4096}[selector]
            assert value % length == 0
            self.memory[value:value + length] = b"\xff" * length
        elif index == 27:
            assert count == 1 and len(write_data) == 512
            assert all(old & new == new for old, new in zip(self.memory[value:value + 512], write_data))
            self.memory[value:value + 512] = write_data
        elif index == 30:
            assert 1 <= count <= 128 and read_size == count * 2
            checks = [crc16(self.memory[pos:pos + 512])
                      for pos in range(value, value + count * 512, 512)]
            if self.corrupt_crc:
                checks[-1] ^= 1
            return struct.pack("<" + "H" * count, *checks)
        elif index != 22:
            raise AssertionError(f"unexpected command {index}")
        return b""


class FlashTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = (Path(__file__).resolve().parents[1] / "header.bin").read_bytes()
        cls.image = parse_dcf(build_dcf(header, bytes(range(256)) * 260))

    def test_commit_last_and_preserve_outside_erase_range(self):
        device = FakeFlash()
        plan = flash_image(device, self.image, len(device.memory))
        writes = [cmd for cmd in device.commands if cmd[0] == 27]
        self.assertEqual(writes[0][1], 512)
        self.assertEqual(writes[-1][1], 0)
        self.assertEqual(device.commands[-2:], [("key",), (22, 0, 0, 0)])
        self.assertEqual(device.memory[:len(self.image.body)], self.image.body)
        self.assertEqual(device.memory[plan.erase_bytes:], b"\x55" * (len(device.memory) - plan.erase_bytes))
        for cmd in device.commands:
            if cmd[0] == 30:
                _, address, selector, count = cmd
                finish = address + count * 512
                self.assertFalse(any(address < boundary < finish
                                     for segment in self.image.segments for boundary in segment))
                self.assertEqual(selector, int(any(start <= address and finish <= end
                                                  for start, end in self.image.segments)))

    def test_crc_failure_leaves_first_block_uncommitted(self):
        device = FakeFlash(corrupt_crc=True)
        with self.assertRaises(VerificationError):
            flash_image(device, self.image, len(device.memory))
        self.assertEqual(device.memory[:512], b"\xff" * 512)
        self.assertFalse(any(cmd[0] == 22 or cmd[:2] == (27, 0) for cmd in device.commands))

    def test_capacity_chip_and_permissions_checked_before_erase(self):
        device = FakeFlash()
        required = plan_flash(self.image).minimum_flash_bytes
        with self.assertRaises(ValueError):
            flash_image(device, self.image, required - 1)
        device.info.chip = "OTHER"
        with self.assertRaises(ValueError):
            flash_image(device, self.image, len(device.memory))
        device.info.chip = "CRWN"
        device.context = RequestContext(operation_mask=0)
        with self.assertRaises(ValueError):
            flash_image(device, self.image, len(device.memory))
        self.assertEqual(device.commands, [])

    def test_failed_erase_completion_stops_before_programming(self):
        device = FakeFlash()
        device.ping = lambda: False
        with self.assertRaises(RuntimeError):
            flash_image(device, self.image, len(device.memory))
        self.assertEqual(len(device.commands), 1)


if __name__ == "__main__":
    unittest.main()
