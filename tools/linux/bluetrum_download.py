"""CRWN serial ROM/loader protocol recovered from Downloader.exe 1.9.7.

Method RIDs refer to the decrypted vendor IL, not its on-disk dummy bodies.
Only the ordinary UART adapter, 115200-baud path is implemented. No serial port
is opened by importing this module. Hardware validation is still outstanding.
"""
from dataclasses import dataclass, replace
import secrets
import struct
import time

from bluetrum_uart import RequestContext, build_request, calculate_key, crc16, encode_ram, unpack_loader


class ProtocolError(RuntimeError):
    pass


class FramedUart:
    """RID 1249/1251/1253/1255/1257; transport is a SerialPort-like object."""
    def __init__(self, transport, *, echo=True, retries=3, timeout=2.0):
        if retries < 1 or timeout <= 0:
            raise ValueError("positive retry count and timeout required")
        self.transport = transport
        self.echo = echo
        self.retries = retries
        self.timeout = timeout
        self.sequence = 0

    def _next_sequence(self):
        self.sequence = (self.sequence + 1) & 255
        return self.sequence

    def _write(self, data):
        self.transport.write_all(data, timeout=self.timeout)
        if self.echo:
            echo = self.transport.read_exact(len(data), timeout=self.timeout)
            if echo != data:
                raise ProtocolError("UART local echo mismatch; check adapter wiring/--no-echo")

    def synchronize(self, *, timeout=10.0):
        """RID 1244 normal UART path: A596875A -> 5A6978A5."""
        if timeout <= 0:
            raise ValueError("positive synchronization timeout required")
        self.sequence = 0
        self.transport.set_baud(115200)
        self.transport.flush_input()
        deadline = time.monotonic() + timeout
        window = bytearray()
        while time.monotonic() < deadline:
            try:
                self._write(bytes.fromhex("a596875a"))
                until = min(deadline, time.monotonic() + .05)
                while time.monotonic() < until:
                    part = self.transport.read_some(64, timeout=max(.001, until-time.monotonic()))
                    window.extend(part)
                    if bytes.fromhex("5a6978a5") in window:
                        return
                    del window[:-3]
            except TimeoutError:
                pass
        raise TimeoutError("ROM sync timed out; reset/power the target into its download mode")

    def send(self, payload):
        """Retry the same sequence only after an explicit negative ACK.

        A missing ACK leaves acceptance ambiguous; abort rather than re-send a
        flash block. The vendor also recognizes 3C as a successful duplicate.
        """
        if not 0 < len(payload) <= 512:
            raise ValueError("UART payload must contain 1..512 bytes")
        seq = self._next_sequence()
        frame = struct.pack("<BBH", 0x4b, seq, len(payload)) + payload + struct.pack("<H", crc16(payload))
        for _ in range(self.retries):
            self._write(frame)
            status, got_seq = self.transport.read_exact(2, timeout=self.timeout)
            if got_seq != seq:
                raise ProtocolError("UART ACK sequence mismatch")
            if status in (0x1e, 0x3c):
                return
            if status != 0x2d:
                raise ProtocolError(f"unexpected UART ACK {status:02x}")
        raise ProtocolError("target rejected UART packet")

    def receive(self, length):
        if not 0 < length <= 512:
            raise ValueError("UART response must contain 1..512 bytes")
        seq = self._next_sequence()
        for _ in range(self.retries):
            self._write(bytes((0xb4, seq)))
            status, got_seq = self.transport.read_exact(2, timeout=self.timeout)
            if got_seq != seq:
                raise ProtocolError("UART response sequence mismatch")
            if status == 0x2d:
                continue
            if status != 0x4b:
                raise ProtocolError(f"unexpected UART response {status:02x}")
            size, = struct.unpack("<H", self.transport.read_exact(2, timeout=self.timeout))
            if size > 542:
                raise ProtocolError("oversized UART response")
            data = self.transport.read_exact(size + 2, timeout=self.timeout)
            if size != length:
                raise ProtocolError(f"UART response length {size}, expected {length}")
            if crc16(data[:-2]) != int.from_bytes(data[-2:], "little"):
                raise ProtocolError("UART response CRC mismatch")
            return data[:-2]
        raise ProtocolError("target did not provide UART data")

    def exchange(self, command, *, read_size=0, write_data=b""):
        if read_size < 0 or (read_size and write_data):
            raise ValueError("choose either a read or a write transfer")
        self.send(command)
        for offset in range(0, len(write_data), 512):
            self.send(write_data[offset:offset+512])
        return b"".join(self.receive(min(512, read_size-offset))
                        for offset in range(0, read_size, 512))


@dataclass(frozen=True)
class BootInfo:
    chip: str
    version: int
    ram_address: int
    status: int
    baud_feature: int


@dataclass(frozen=True)
class FlashInfo:
    flash_size: int
    jedec_id: int
    flash_type: int
    operation_mask: int
    device_info: bytes


def flash_capacity(jedec):
    """Exact recognized IDs in RID 528 (middle byte and low byte ignored)."""
    value = jedec & 0xff00ff00
    manufacturer, density = value >> 24, (value >> 8) & 255
    supported = {0x0b: range(0x12, 0x16), 0x1c: range(0x12, 0x16),
                 0x5e: range(0x12, 0x16), 0x85: range(0x12, 0x16),
                 0xc2: range(0x12, 0x16), 0xeb: range(0x12, 0x16),
                 0xef: range(0x12, 0x19)}
    if value == 0x0b00ff00:  # Special vendor ID, mapped to 256 KiB.
        return 1 << 18
    if manufacturer not in supported or density not in supported[manufacturer]:
        raise ProtocolError(f"flash ID {jedec:08x} is not in the vendor capacity table")
    return 1 << density


class Bootloader:
    def __init__(self, link):
        self.link = link
        self.context = RequestContext()
        self.info = None
        self.flash_info = None
        self._transactions = 0
        self._loaded = False

    @property
    def session_key(self):
        return self.context.session_key

    def _exchange(self, command, *, read_size=0, write_data=b"", counted=True):
        response = self.link.exchange(command, read_size=read_size, write_data=write_data)
        if counted:
            self._transactions += 1
            if self._transactions >= 10:
                self.refresh_key()
                self._transactions = 0
        return response

    def request(self, index, *, value=0, selector=0, count=0, read_size=0, write_data=b""):
        command = build_request(index, value=value, selector=selector, count=count, context=self.context)
        # RID 566 reboot uses the uncounted transport: the target is gone
        # after acceptance, so a periodic key refresh cannot follow it.
        return self._exchange(command, read_size=read_size, write_data=write_data, counted=index != 22)

    def refresh_key(self, value=0):
        """RID 535: key rotation itself is excluded from RID 519's counter."""
        command = build_request(3, value=value, context=self.context)
        key = int.from_bytes(self._exchange(command, read_size=4, counted=False), "big")
        if not key:
            raise ProtocolError("target returned a zero session key")
        self.context = replace(self.context, session_key=key)
        return key

    def _hello(self, nonce=0):
        command = build_request(1, selector=1, count=nonce, context=self.context)
        return self._exchange(command, read_size=24, counted=False)

    def ping(self):
        response = self._hello()
        return response[:4] == b"BLUE"

    def connect(self, *, timeout=10.0):
        self.link.synchronize(timeout=timeout)
        time.sleep(.01)
        response = self._hello(secrets.randbelow(65535) + 1)
        if response[:4] != b"BLUE":
            raise ProtocolError("ROM hello does not contain BLUE magic")
        chip = response[4:8].decode("ascii", errors="replace")
        version, address, challenge = struct.unpack_from(">III", response, 8)
        if chip != "CRWN" or version != 0x01000000:
            raise ProtocolError(f"this implementation supports CRWN ROM 01000000, got {chip} {version:08x}")
        self.info = BootInfo(chip, version, address, response[22], response[23])
        self.context = RequestContext(session_key=challenge, ram_key=address)
        self.refresh_key(calculate_key(challenge))
        return self.info

    def load_loader(self, container):
        """RID 549: encrypted RAM write, key rotation, read-back, then jump."""
        if self.info is None:
            raise ProtocolError("connect before loading the RAM driver")
        payload = unpack_loader(container)
        if len(payload) > 65528:
            raise ValueError("loader exceeds vendor 64-KiB container limit")
        padded = payload + bytes(-len(payload) % 512)
        count = len(padded) // 512
        address = self.context.ram_key
        self.request(5, value=address, selector=1, count=count,
                     write_data=encode_ram(padded, self.context))
        self.refresh_key()
        self._transactions = 0
        data = self.request(4, value=address, selector=1, count=count, read_size=len(padded))
        # RID 549 compares only the original code length; rounded transfer
        # padding is outside the loader image and is not part of verification.
        if encode_ram(data, self.context)[:len(payload)] != payload:
            raise ProtocolError("RAM loader read-back verification failed")
        self.request(6, value=address)
        self._loaded = True

    def initialize_flash(self):
        """RID 527/551/553. Preserve reported permissions; no unlock attempt."""
        if not self._loaded:
            raise ProtocolError("load the RAM driver before initializing flash")
        response = self.request(16, read_size=4)
        flash_type = response[1]
        if not flash_type:
            raise ProtocolError("loader did not detect a flash interface")
        self.context = replace(self.context, operation_mask=response[2], ram_type=flash_type)
        device_info = self.request(25, read_size=16)
        if flash_type == 128 or flash_type & 15 == 4:
            raise ProtocolError("RAM/alternate storage mode is outside the CRWN SPI flash implementation")
        jedec = int.from_bytes(self.request(21, read_size=4), "big")
        size = flash_capacity(jedec)
        self.flash_info = FlashInfo(size, jedec, flash_type, self.context.operation_mask, device_info)
        return self.flash_info
