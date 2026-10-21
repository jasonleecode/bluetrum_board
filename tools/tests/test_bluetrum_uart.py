#!/usr/bin/env python3
"""Run with unittest; optional Unicorn runs the original x86 DLL as an oracle.

PYTHONPATH=/tmp/ab32-recovery-deps python3 -m unittest discover \
    -s tools/tests -p test_bluetrum_uart.py -v
"""
from pathlib import Path
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux"))
import bluetrum_uart as protocol

ROOT = Path(__file__).resolve().parents[2]
DLLS = ROOT / "tools/windows/downloader/dlls"


class ProtocolTests(unittest.TestCase):
    def test_standard_crc_vectors(self):
        self.assertEqual(protocol.crc16(b"123456789"), 0x29b1)
        self.assertEqual(protocol.crc32(b"123456789"), 0x340bc6d9)

    def test_all_bundled_loaders_verify(self):
        files = sorted(DLLS.glob("*down-*.dll"))
        self.assertEqual(len(files), 17)
        for file in files:
            with self.subTest(file=file.name):
                container = file.read_bytes()
                payload = protocol.unpack_loader(container)
                self.assertEqual(len(payload), len(container) - 8)
                self.assertEqual(payload[0] & 0x7f, 0x6f)  # RISC-V JAL
                corrupt = bytearray(container)
                corrupt[-1] ^= 1
                with self.assertRaisesRegex(ValueError, "CRC mismatch"):
                    protocol.unpack_loader(corrupt)

    def test_crwn_loader(self):
        self.assertEqual(protocol.loader_name("CRWN"), "uartdown-001.dll")
        info = protocol.inspect_loader(DLLS / "uartdown-001.dll")
        self.assertEqual(info["payload_bytes"], 9408)
        self.assertEqual(info["crc16"], "a0c2")
        self.assertEqual(info["first_16_bytes"], "6f0000262328a0068327c0061397f700")

    def test_request_bounds_and_templates(self):
        self.assertEqual(protocol.build_request(1), bytes.fromhex("5a5259414e000000"))
        for index in (-1, 0, 9, 47, 48):
            with self.assertRaises(ValueError):
                protocol.build_request(index)
        with self.assertRaises(ValueError):
            protocol.build_request(2, count=65536)
        with self.assertRaises(ValueError):
            protocol.build_request(0x1a, value=1025)
        with self.assertRaises(ValueError):
            protocol.build_request(0x1b)
        with self.assertRaises(ValueError):
            protocol.unpack_loader(b"bad")


try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX
except ImportError:
    Uc = None


@unittest.skipIf(Uc is None, "install Unicorn to run original DLL differential tests")
class OriginalDllTests(unittest.TestCase):
    """No Windows process, serial port or device access occurs in these tests."""

    def setUp(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        data = (DLLS / "DeviceRequest.dll").read_bytes()
        pe, = struct.unpack_from("<I", data, 0x3c)
        sections, = struct.unpack_from("<H", data, pe + 6)
        optsize, = struct.unpack_from("<H", data, pe + 20)
        self.uc.mem_map(0x10000000, 0x10000)
        for index in range(sections):
            section = pe + 24 + optsize + index * 40
            _, rva, size, offset = struct.unpack_from("<4I", data, section + 8)
            self.uc.mem_write(0x10000000 + rva, data[offset:offset + size])
        # DLL startup verifies its host EXE digest. Unit testing pure functions
        # supplies its initialized marker instead of executing the Windows host.
        self.uc.mem_write(0x100070fc, b"Hugo")
        self.uc.mem_map(0x20000000, 0x1000)
        self.uc.mem_map(0x30000000, 0x10000)
        self.uc.mem_map(0x40000000, 0x20000)
        self.uc.hook_add(UC_HOOK_CODE, self._hook)
        self.buffer = 0x40001000
        self.ctx = 0x40000000

    def _hook(self, uc, address, size, user):
        if address == 0x10002e10:  # imported memcpy; snapshot permits overlap
            esp = uc.reg_read(UC_X86_REG_ESP)
            ret, dst, src, length = struct.unpack("<4I", uc.mem_read(esp, 16))
            uc.mem_write(dst, bytes(uc.mem_read(src, length)))
            uc.reg_write(UC_X86_REG_EAX, dst)
            uc.reg_write(UC_X86_REG_ESP, esp + 4)
            uc.reg_write(UC_X86_REG_EIP, ret)

    def call(self, rva, *args):
        stack = 0x3000f000
        self.uc.mem_write(stack, struct.pack("<" + "I" * (len(args) + 1),
                                            0x20000000, *args))
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.emu_start(0x10000000 + rva, 0x20000000, count=10000000)
        self.assertEqual(self.uc.reg_read(UC_X86_REG_EIP), 0x20000000)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def set_context(self, context):
        fields = bytearray(64)
        for offset, value in ((0xc, context.operation_mask), (0x10, context.ram_key),
                              (0x14, context.session_key), (0x3c, context.ram_type)):
            struct.pack_into("<I", fields, offset, value)
        for bit in range(4):
            struct.pack_into("<I", fields, 0x24 + bit * 4,
                             (context.operation_mask >> bit) & 1)
        self.uc.mem_write(self.ctx, bytes(fields))

    def test_original_crc_stream_key_and_ram(self):
        rng = random.Random(0x197)
        for length in (0, 1, 2, 7, 8, 9, 255, 512, 4096):
            data = bytes(rng.randrange(256) for _ in range(length))
            seed = rng.getrandbits(32)
            self.uc.mem_write(self.buffer, data or b"\0")
            self.assertEqual(self.call(0x1000, self.buffer, 0, length, seed) & 0xffff,
                             protocol.crc16(data, seed))
            self.assertEqual(self.call(0x1060, self.buffer, 0, length, seed),
                             protocol.crc32(data, seed))
            self.assertEqual(self.call(0x1100, self.buffer, 0, length) & 0xff,
                             protocol.checksum8(data))
            self.call(0x10b0, self.buffer, 0, length, seed)
            self.assertEqual(bytes(self.uc.mem_read(self.buffer, length)),
                             protocol.xor_stream(data, seed))
            self.assertEqual(self.call(0x1160, seed), protocol.calculate_key(seed))
            context = protocol.RequestContext(seed, rng.getrandbits(32))
            self.set_context(context)
            self.uc.mem_write(self.buffer, data or b"\0")
            self.call(0x1380, self.ctx, self.buffer, length)
            self.assertEqual(bytes(self.uc.mem_read(self.buffer, length)),
                             protocol.encode_ram(data, context))

    def test_original_all_request_indices(self):
        rng = random.Random(0x5259414e)
        for flags in (0, 1, 2, 4, 8, 15, 31):
            for ram_type in (0, 4):
                context = protocol.RequestContext(rng.getrandbits(32), 0, flags, ram_type)
                self.set_context(context)
                for index in range(48):
                    value, selector, count = rng.randrange(4096), rng.randrange(256), rng.randrange(5)
                    template = bytes(rng.randrange(256) for _ in range(8))
                    self.uc.mem_write(self.buffer, template + b"\0")
                    size = self.call(0x12b0, self.ctx, self.buffer, index, selector, value, count)
                    with self.subTest(flags=flags, ram_type=ram_type, index=index):
                        if size == 0:
                            with self.assertRaises(ValueError):
                                protocol.build_request(index, value, selector, count, context, template)
                        else:
                            self.assertIn(size, (8, 9))
                            self.assertEqual(bytes(self.uc.mem_read(self.buffer, size)),
                                             protocol.build_request(index, value, selector, count,
                                                                    context, template))

    def test_original_authentication(self):
        rng = random.Random(0x1440)
        for _ in range(32):
            challenge = bytes(rng.randrange(256) for _ in range(4))
            identity = bytes(rng.randrange(256) for _ in range(12))
            context = protocol.RequestContext(rng.getrandbits(32))
            self.set_context(context)
            self.uc.mem_write(self.buffer + 64, challenge)
            self.uc.mem_write(self.buffer + 128, identity)
            self.call(0x1440, self.ctx, self.buffer, self.buffer + 64, self.buffer + 128)
            self.assertEqual(bytes(self.uc.mem_read(self.buffer, 18)),
                             protocol.build_authentication(challenge, identity, context))

    def test_original_unpack_all_loaders(self):
        for file in sorted(DLLS.glob("*down-*.dll")):
            data = file.read_bytes()
            self.uc.mem_write(self.buffer, data)
            self.assertEqual(self.call(0x13b0, self.buffer, len(data)), 1)
            self.assertEqual(bytes(self.uc.mem_read(self.buffer, len(data) - 8)),
                             protocol.unpack_loader(data))


if __name__ == "__main__":
    unittest.main()
