#!/usr/bin/env python3
"""Recovered Downloader 1.9.7 protocol primitives (offline, no serial access).

Source: tools/windows/downloader/dlls/DeviceRequest.dll, image base 0x10000000.
The transport and recovered ROM/loader state machine live separately in
bluetrum_download.py; this module keeps the byte transformations pure.
The vendor's START_UPD^_^ updater protocol is a separate application protocol.
"""

import argparse
import binascii
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct


COMMAND_MAP = bytes.fromhex(
    "ff5a5055525758515effffffffffffff0010151a1c1d1e202225282a2d2e2fff27"
    "ffffffffffffffffffffffffffffff"
)
UART_LOADERS = dict(zip(
    ("CRWN", "DREM", "EPIC", "GOAL", "HONR", "IDEA", "JAZZ", "KING",
     "LUCK", "MAGI", "NOVA", "OSCR", "ABLE", "PRAO"),
    ("001", "002", "003", "005", "006", "007", "008", "009", "010",
     "011", "012", "013", "T01", "014"),
))


def crc16(data: bytes, initial: int = 0xffff) -> int:
    """dr_calc_crc RVA 0x1000: CRC-16/CCITT, no final xor."""
    return binascii.crc_hqx(data, initial & 0xffff)


def crc32(data: bytes, initial: int = 0xffffffff) -> int:
    """dr_calc_crc32 RVA 0x1060: reflected IEEE CRC, no final xor."""
    return binascii.crc32(data, (initial ^ 0xffffffff) & 0xffffffff) ^ 0xffffffff


def checksum8(data: bytes) -> int:
    """Low byte of dr_calc_chksum RVA 0x1100."""
    return -sum(data) & 0xff


def _stream_table() -> tuple:
    values = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0xa3000000 if value & 1 else 0)
        values.append(value)
    return tuple(values)


STREAM_TABLE = _stream_table()


def xor_stream(data: bytes, seed: int) -> bytes:
    """dr_calc_aes RVA 0x10b0; despite its name, this is not AES."""
    state = seed & 0xffffffff
    output = bytearray(data)
    for offset in range(len(output)):
        low = state & 0xff
        output[offset] ^= low
        state = (state >> 8) ^ STREAM_TABLE[low]
    return bytes(output)


def calculate_key(value: int) -> int:
    """dr_calc_key RVA 0x1160."""
    mixed = (value ^ 0x5555aaaa) & 0xffffffff
    high = crc16(struct.pack("<I", mixed))
    low = crc16(struct.pack("<I", mixed ^ 0xffffffff))
    return high << 16 | low


def unpack_loader(container: bytes) -> bytes:
    """dr_check_dll RVA 0x13b0: strip 8-byte header and verify plaintext CRC.

    Bytes 4:6 contain a CRC, not a length. Bytes 6:8 are ignored by the DLL.
    uartdown/usbdown files contain target RISC-V code, not Windows DLL code.
    """
    if len(container) <= 8 or container[:4] != b"dll\0":
        raise ValueError("not a downloader loader container (expected dll\\0 header)")
    expected, = struct.unpack_from("<H", container, 4)
    plaintext = xor_stream(container[8:], expected ^ 0x48502018)
    actual = crc16(plaintext)
    if actual != expected:
        raise ValueError(f"loader CRC mismatch: expected {expected:04x}, got {actual:04x}")
    return plaintext


def loader_name(chip: str) -> str:
    """dr_get_uartdrv_path RVA 0x1630; tags written in human-readable order."""
    try:
        return "uartdown-" + UART_LOADERS[chip.upper()] + ".dll"
    except KeyError:
        raise ValueError(f"unsupported chip tag {chip!r}") from None


@dataclass(frozen=True)
class RequestContext:
    """Recovered DLL context fields. ram_key is the ROM-reported RAM load address.

    session_key: DWORD +0x14; ram_key: DWORD +0x10; operation_mask: +0x0c
    and its individual flags at +0x24/+0x28/+0x2c/+0x30; ram_type: +0x3c.
    """
    session_key: int = 0
    ram_key: int = 0
    operation_mask: int = 0
    ram_type: int = 0


def build_request(index: int, value: int = 0, selector: int = 0,
                  count: int = 0, context: RequestContext = RequestContext(),
                  template: bytes = bytes(8)) -> bytes:
    """dr_set_req RVA 0x12b0, with the checks in helper RVA 0x11c0.

    `index` is the DLL command index, not the on-wire opcode. The plaintext
    layout is opcode:u8, value:u32be, selector:u8, count:u16be. Index 1
    substitutes 0x5259414e (RYAN) for value. Opcode 0x2e preserves template
    bytes 1:8. This function constructs bytes only; command sequencing and
    acknowledgements are implemented in bluetrum_download.py.
    """
    if not 0 <= index < len(COMMAND_MAP) or COMMAND_MAP[index] == 0xff:
        raise ValueError("unsupported command index")
    if not 0 <= value <= 0xffffffff or not 0 <= selector <= 0xff or not 0 <= count <= 0xffff:
        raise ValueError("request fields do not fit their wire widths")
    if len(template) != 8:
        raise ValueError("request template must contain exactly 8 bytes")
    opcode = COMMAND_MAP[index]
    flags = context.operation_mask & 0xf
    if opcode == 0x28 and context.ram_type & 0xf != 4 and not flags & 1:
        if value + (count << 9) > 1024:
            raise ValueError("DLL rejects this range without operation flag 0")
    required_flag = {0x2a: 2, 0x2d: 4, 0x2f: 8}.get(opcode)
    if required_flag and (flags != context.operation_mask or not flags & required_flag):
        raise ValueError("DLL rejects this request without its operation flag")
    if opcode == 0x5a:
        value = 0x5259414e
    frame = bytearray(template)
    frame[0] = opcode
    if opcode != 0x2e:
        frame[1:] = struct.pack(">IBH", value, selector, count)
    if opcode in (0x5a, 0x1e):
        return bytes(frame)
    check = checksum8(frame)
    frame = bytearray(xor_stream(frame, context.session_key ^ check))
    frame[0] |= 0x80
    return bytes(frame) + bytes([check])


def encode_ram(data: bytes, context: RequestContext) -> bytes:
    """dr_set_ram RVA 0x1380 (symmetric)."""
    return xor_stream(data, context.session_key ^ context.ram_key)


def build_authentication(challenge: bytes, identity: bytes,
                         context: RequestContext) -> bytes:
    """dr_set_authent RVA 0x1440; identity is the 12-byte input structure.

    The original mutates its four-byte challenge buffer; this pure function
    returns only the 18-byte output, preserving the caller's input.
    """
    if len(challenge) != 4 or len(identity) != 12:
        raise ValueError("authentication requires a 4-byte challenge and 12-byte identity")
    key = struct.pack(">I", context.session_key & 0xffffffff)
    body = challenge + bytes(a ^ b for a, b in zip(challenge, key)) + identity[4:12]
    return body + struct.pack(">H", crc16(body))


def inspect_loader(path: Path) -> dict:
    container = path.read_bytes()
    plaintext = unpack_loader(container)
    return {"path": str(path), "payload_bytes": len(plaintext),
            "crc16": f"{crc16(plaintext):04x}",
            "sha256": hashlib.sha256(plaintext).hexdigest(),
            "first_16_bytes": plaintext[:16].hex(),
            "kind": "target RAM loader; not a host DLL"}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect = sub.add_parser("inspect-loader", help="decrypt and check a loader without writing files")
    inspect.add_argument("file", type=Path)
    unpack = sub.add_parser("unpack-loader", help="extract and CRC-check target RAM code")
    unpack.add_argument("file", type=Path)
    unpack.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        info = inspect_loader(args.file)
        if args.command == "unpack-loader":
            with args.output.open("xb") as output:
                output.write(unpack_loader(args.file.read_bytes()))
            info["output"] = str(args.output)
        print(json.dumps(info, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
