"""Offline protocol transcripts; no serial ports or target hardware are used."""
import dataclasses
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from bluetrum_download import Bootloader, FramedUart, ProtocolError, flash_capacity
from bluetrum_uart import RequestContext, build_request, encode_ram


class TranscriptPort:
    def __init__(self, steps):
        self.steps = list(steps)
        self.pending = b''
        self.writes = []

    def write_all(self, data, timeout=None):
        expected, answer = self.steps.pop(0)
        if data != expected:
            raise AssertionError(f'wire: {data.hex()}, expected {expected.hex()}')
        self.writes.append(data)
        self.pending += answer

    def read_exact(self, size, timeout=None):
        if len(self.pending) < size:
            raise TimeoutError('simulated missing ACK')
        result, self.pending = self.pending[:size], self.pending[size:]
        return result


class FramingTest(unittest.TestCase):
    def test_vendor_frame_crc_sequence_and_echo(self):
        # CRC-16/CCITT("123456789") = 29b1; vendor frame stores it LE.
        frame = bytes.fromhex('4b010900313233343536373839b129')
        port = TranscriptPort([(frame, frame + b'\x1e\x01'),
                               (b'\xb4\x02', b'\xb4\x02' + bytes.fromhex('4b020900313233343536373839b129'))])
        link = FramedUart(port)
        self.assertEqual(link.exchange(b'123456789', read_size=9), b'123456789')
        self.assertFalse(port.steps)

    def test_explicit_nack_reuses_sequence(self):
        frame = bytes.fromhex('4b010900313233343536373839b129')
        port = TranscriptPort([(frame, b'\x2d\x01'), (frame, b'\x3c\x01')])
        link = FramedUart(port, echo=False)
        link.send(b'123456789')
        self.assertEqual(link.sequence, 1)

    def test_missing_ack_does_not_resend_flash_data(self):
        frame = bytes.fromhex('4b010900313233343536373839b129')
        port = TranscriptPort([(frame, b'')])
        with self.assertRaises(TimeoutError):
            FramedUart(port, echo=False).send(b'123456789')
        self.assertEqual(len(port.writes), 1)

    def test_reject_sequence_length_crc_and_echo(self):
        for answer in [bytes.fromhex('4b020900313233343536373839b129'),
                       bytes.fromhex('4b010900313233343536373839b128'),
                       bytes.fromhex('4b01010000f0e1'), bytes.fromhex('4b01ffff')]:
            with self.subTest(answer=answer.hex()):
                port = TranscriptPort([(b'\xb4\x01', answer)])
                with self.assertRaises(ProtocolError):
                    FramedUart(port, echo=False).receive(9)


class CommandTranscript:
    def __init__(self, steps):
        self.steps = list(steps)
        self.synced = False

    def synchronize(self, timeout):
        self.synced = True

    def exchange(self, command, *, read_size=0, write_data=b''):
        expected, expected_read, expected_write, result = self.steps.pop(0)
        if (command, read_size, write_data) != (expected, expected_read, expected_write):
            raise AssertionError('unexpected command/transfer in recovered IL transcript')
        return result


class BootTest(unittest.TestCase):
    def connected(self):
        # RID 534: plaintext RYAN, selector1, nonce=1. RID535 golden
        # request bytes are covered independently by original DLL oracle tests.
        reply = b'BLUECRWN' + struct.pack('>III', 0x01000000, 0x12000, 0x12345678) + b'\0\0\0\0'
        from bluetrum_uart import calculate_key
        key_command = build_request(3, value=calculate_key(0x12345678), context=RequestContext(session_key=0x12345678))
        link = CommandTranscript([(b'\x5aRYAN\x01\0\x01', 24, b'', reply),
                                  (key_command, 4, b'', bytes.fromhex('11223344'))])
        boot = Bootloader(link)
        with patch('bluetrum_download.secrets.randbelow', return_value=0):
            info = boot.connect()
        self.assertEqual((info.chip, info.ram_address), ('CRWN', 0x12000))
        self.assertEqual(boot.session_key, 0x11223344)
        return boot, link

    def test_connect_and_vendor_loader_sequence(self):
        boot, link = self.connected()
        container = (Path(__file__).resolve().parents[1] / 'windows/downloader/dlls/uartdown-001.dll').read_bytes()
        from bluetrum_uart import unpack_loader
        payload = unpack_loader(container)
        padded = payload + bytes(-len(payload) % 512)
        count = len(padded) // 512
        context = boot.context
        next_context = dataclasses.replace(context, session_key=0x55667788)
        link.steps.extend([
            (build_request(5, value=0x12000, selector=1, count=count, context=context), 0, encode_ram(padded, context), b''),
            (build_request(3, context=context), 4, b'', bytes.fromhex('55667788')),
            (build_request(4, value=0x12000, selector=1, count=count, context=next_context), len(padded), b'', encode_ram(padded, next_context)),
            (build_request(6, value=0x12000, context=next_context), 0, b'', b''),
        ])
        boot.load_loader(container)
        self.assertFalse(link.steps)

    def test_loader_checks_code_but_ignores_transfer_padding(self):
        container = (Path(__file__).resolve().parents[1] / 'windows/downloader/dlls/uartdown-001.dll').read_bytes()
        from bluetrum_uart import unpack_loader
        payload = unpack_loader(container)
        padded = payload + bytes(-len(payload) % 512)
        self.assertGreater(len(padded), len(payload))
        for position, accepted in ((len(padded) - 1, True), (len(payload) - 1, False)):
            with self.subTest(position=position):
                boot, link = self.connected()
                next_context = dataclasses.replace(boot.context, session_key=0x55667788)
                changed = bytearray(padded)
                changed[position] ^= 0xff
                responses = [b'', bytes.fromhex('55667788'), encode_ram(changed, next_context), b'']
                with patch.object(link, 'exchange', side_effect=responses) as exchange:
                    if accepted:
                        boot.load_loader(container)
                        self.assertEqual(exchange.call_count, 4)  # Jump follows successful verification.
                    else:
                        with self.assertRaisesRegex(ProtocolError, 'read-back verification failed'):
                            boot.load_loader(container)
                        self.assertEqual(exchange.call_count, 3)  # Never execute corrupted code.

    def test_rotate_after_tenth_transaction_preserves_response(self):
        boot, link = self.connected()
        command = build_request(25, context=boot.context)
        link.steps.extend([(command, 16, b'', bytes(range(16)))] * 10)
        link.steps.append((build_request(3, context=boot.context), 4, b'', bytes.fromhex('99887766')))
        for _ in range(10):
            self.assertEqual(boot.request(25, read_size=16), bytes(range(16)))
        self.assertEqual(boot.session_key, 0x99887766)
        self.assertEqual(boot._transactions, 0)

    def test_reboot_does_not_rotate_key_after_acceptance(self):
        boot, link = self.connected()
        boot._transactions = 9
        link.steps.append((build_request(22, context=boot.context), 0, b'', b''))
        boot.request(22)
        self.assertEqual(boot._transactions, 9)
        self.assertFalse(link.steps)

    def test_initialize_preserves_permission_mask(self):
        boot, link = self.connected()
        boot._loaded = True
        context = boot.context
        with_mask = dataclasses.replace(context, operation_mask=1, ram_type=1)
        link.steps.extend([(build_request(16, context=context), 4, b'', b'\0\x01\x01\0'),
                           (build_request(25, context=with_mask), 16, b'', bytes(16)),
                           (build_request(21, context=with_mask), 4, b'', bytes.fromhex('ef401500'))])
        info = boot.initialize_flash()
        self.assertEqual(info.flash_size, 2*1024*1024)
        self.assertEqual(boot.context.operation_mask, 1)
        with self.assertRaises(ValueError):
            boot.request(27, count=1, write_data=bytes(512))
        self.assertFalse(link.steps)

    def test_exact_vendor_capacity_ranges(self):
        for maker in (0x0b, 0x1c, 0x5e, 0x85, 0xc2, 0xeb, 0xef):
            for density in range(0x12, 0x16):
                self.assertEqual(flash_capacity((maker << 24) | (density << 8) | 0x00400000), 1 << density)
        self.assertEqual(flash_capacity(0x0b00ff00), 262144)
        self.assertEqual(flash_capacity(0xef401800), 16777216)
        for value in (0, 0xc2401600, 0xef401900, 0xaa401500):
            with self.assertRaises(ProtocolError):
                flash_capacity(value)


if __name__ == '__main__':
    unittest.main()
