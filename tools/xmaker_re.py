#!/usr/bin/env python3
"""Mac-side helpers for studying Bluetrum xmaker files.

This is not yet a full replacement for riscv32-elf-xmaker.exe. It provides the
verified primitives currently recovered from the Windows binary: XM decoding,
PE table extraction, xmaker CRC16, the rolling XOR stream transform, and an
experimental inner-body builder/checker.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


IMAGE_BASE = 0x400000
CRC16_TABLE_VA = 0x43DC78
XOR32_TABLE_VA = 0x43DE78
HEADER_SIZE = 4096
FIRST_BODY_SIZE = 8192
XMAKER_BLOCK_SIZE = 512
XFIL_KEY = 0x4C494658
XAPP_KEY = 0x50504158
LVMG_KEY = 0x474D564C
SEG_KEY_XOR = 0x6B676573


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def ensure_len(buf: bytearray, size: int, fill: int = 0xFF) -> None:
    if len(buf) < size:
        buf.extend(bytes([fill]) * (size - len(buf)))


def read_u16le(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def read_u32le(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def put_u16le(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buf, offset, value & 0xFFFF)


def put_u32le(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, value & 0xFFFFFFFF)


def put_tag(buf: bytearray, offset: int, tag: str, highbit: bool = True) -> None:
    raw = highbit_tag_bytes(tag) if highbit else tag.encode("ascii")
    if len(raw) != 4:
        raise ValueError(f"tag must encode to 4 bytes: {tag!r}")
    buf[offset : offset + 4] = raw


def duplicate_u16(value: int) -> int:
    value &= 0xFFFF
    return value | (value << 16)


def xor_stream_inplace(buf: bytearray, offset: int, length: int, key: int, table: list[int]) -> None:
    transformed, _final_key = xmaker_xor_stream(bytes(buf[offset : offset + length]), key, table)
    buf[offset : offset + length] = transformed


def highbit_tag_bytes(tag: str) -> bytes:
    raw = tag.encode("ascii")
    return bytes((byte ^ 0x80) if byte else 0 for byte in raw)


def highbit_tag_value(tag: str) -> int:
    return int.from_bytes(highbit_tag_bytes(tag), "little")


def read_file(path: Path) -> bytes:
    return path.read_bytes()


def decode_xm(data: bytes) -> str:
    if data.startswith(b"\xff\xfe"):
        return data.decode("utf-16le")
    return data.decode("utf-8")


def pe_sections(data: bytes) -> list[tuple[str, int, int, int]]:
    pe_off = data.find(b"PE\0\0")
    if pe_off < 0:
        raise ValueError("not a PE file")

    section_count = struct.unpack_from("<H", data, pe_off + 6)[0]
    optional_header_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    section_off = pe_off + 24 + optional_header_size

    sections: list[tuple[str, int, int, int]] = []
    for index in range(section_count):
        off = section_off + index * 40
        name = data[off : off + 8].rstrip(b"\0").decode("ascii", errors="replace")
        _virtual_size, virtual_addr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, IMAGE_BASE + virtual_addr, raw_ptr, raw_size))

    return sections


def read_pe_va(data: bytes, va: int, size: int) -> bytes:
    for _name, section_va, raw_ptr, raw_size in pe_sections(data):
        if section_va <= va < section_va + raw_size:
            off = raw_ptr + (va - section_va)
            return data[off : off + size]
    raise ValueError(f"VA 0x{va:x} is outside mapped raw sections")


def extract_crc16_table(exe_data: bytes) -> list[int]:
    raw = read_pe_va(exe_data, CRC16_TABLE_VA, 256 * 2)
    return list(struct.unpack("<256H", raw))


def extract_xor32_table(exe_data: bytes) -> list[int]:
    raw = read_pe_va(exe_data, XOR32_TABLE_VA, 256 * 4)
    return list(struct.unpack("<256I", raw))


def xmaker_crc16(data: bytes, seed: int = 0xFFFF, table: list[int] | None = None) -> int:
    if table is None:
        table = make_crc16_table()

    crc = seed & 0xFFFF
    for byte in data:
        index = byte ^ (crc >> 8)
        crc = ((crc << 8) ^ table[index]) & 0xFFFF
    return crc


def make_crc16_table() -> list[int]:
    table: list[int] = []
    for index in range(256):
        crc = index << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
        table.append(crc)
    return table


def xmaker_xor_stream(data: bytes, key: int, table: list[int]) -> tuple[bytes, int]:
    key &= 0xFFFFFFFF
    out = bytearray(data)

    for index, byte in enumerate(out):
        out[index] = byte ^ (key & 0xFF)
        key = ((key >> 8) ^ table[key & 0xFF]) & 0xFFFFFFFF

    return bytes(out), key


def decode_highbit_tag(raw: bytes) -> str:
    decoded = bytes((byte ^ 0x80) if byte >= 0x80 else byte for byte in raw)
    return decoded.rstrip(b"\0").decode("ascii", errors="replace")


def build_experimental_body(header: bytes, app: bytes, xor_table: list[int], encrypt: bool = False) -> bytes:
    """Build the confirmed inner body portions of make(header, app).

    This is an intermediate reverse-engineering artifact, not a flashable DCF.
    It covers the fixed header region and the XCOD app record metadata that can
    be validated locally with CRCs. The outer DCF wrapper is intentionally not
    emitted here.
    """

    if len(header) != HEADER_SIZE:
        raise ValueError(f"header size must be {HEADER_SIZE} bytes")

    body = bytearray(header)
    ensure_len(body, FIRST_BODY_SIZE)

    xor_stream_inplace(body, 0, HEADER_SIZE, XFIL_KEY, xor_table)

    block_count = align_up(len(app), XMAKER_BLOCK_SIZE) // XMAKER_BLOCK_SIZE
    payload_size = block_count * XMAKER_BLOCK_SIZE
    record_overhead = align_up(16 + block_count * 2, XMAKER_BLOCK_SIZE)
    record_offset = len(body)
    payload_offset = record_offset + record_overhead
    ensure_len(body, payload_offset + payload_size)

    put_u32le(body, record_offset + 0x00, highbit_tag_value("XCOD"))
    put_u32le(body, record_offset + 0x04, record_overhead)
    put_u32le(body, record_offset + 0x08, payload_size)
    body[record_offset + 0x0C] = 0x10
    body[record_offset + 0x0D] = 0x00
    header_crc = xmaker_crc16(bytes(body[record_offset : record_offset + 0x0E]))
    put_u16le(body, record_offset + 0x0E, header_crc)

    for block_index in range(block_count):
        start = block_index * XMAKER_BLOCK_SIZE
        block = app[start : start + XMAKER_BLOCK_SIZE]
        if len(block) < XMAKER_BLOCK_SIZE:
            block = block + bytes([0xFF]) * (XMAKER_BLOCK_SIZE - len(block))
        block_crc = xmaker_crc16(block, seed=block_index + 1)
        put_u16le(body, record_offset + 0x10 + block_index * 2, block_crc)
        body[payload_offset + start : payload_offset + start + XMAKER_BLOCK_SIZE] = block

    if encrypt:
        app_payload_crc = xmaker_crc16(bytes(body[payload_offset : payload_offset + payload_size]))
        put_u32le(body, 0x40, record_offset)
        put_u32le(body, 0x44, payload_size)
        put_u16le(body, 0x4C, app_payload_crc)
        body[0x4F] = 0x01

        app_info_crc = xmaker_crc16(bytes(body[0x40:0x80]))
        put_u16le(body, 0x80, app_info_crc)
        app_info_key = duplicate_u16(app_info_crc) ^ XAPP_KEY
        xor_stream_inplace(body, 0x40, 0x40, app_info_key, xor_table)

        main_region_crc = xmaker_crc16(bytes(body[0x400:0x1000]))
        put_u16le(body, 0x1C, main_region_crc)
        top_region_crc = xmaker_crc16(bytes(body[0:0x3E]))
        put_u16le(body, 0x3E, top_region_crc)
        xor_stream_inplace(body, 0, 0x40, LVMG_KEY, xor_table)

        body_key_base = duplicate_u16(app_info_crc) ^ LVMG_KEY
        for offset in range(0x400, 0x1000, XMAKER_BLOCK_SIZE):
            block_key = ((offset >> 9) - 1) ^ body_key_base
            xor_stream_inplace(body, offset, XMAKER_BLOCK_SIZE, block_key, xor_table)

        app_key_base = duplicate_u16(app_info_crc) ^ XAPP_KEY
        for block_index in range(block_count):
            block_crc = read_u16le(body, record_offset + 0x10 + block_index * 2)
            block_key = block_crc ^ app_key_base
            offset = payload_offset + block_index * XMAKER_BLOCK_SIZE
            xor_stream_inplace(body, offset, XMAKER_BLOCK_SIZE, block_key, xor_table)

    return bytes(body)


def inspect_experimental_body(
    data: bytes,
    record_offset: int = FIRST_BODY_SIZE,
    encrypted: bool = False,
    xor_table: list[int] | None = None,
) -> list[str]:
    lines: list[str] = []
    if len(data) < record_offset + 16:
        raise ValueError("body is too small to contain the requested record")

    raw_tag = data[record_offset : record_offset + 4]
    overhead = read_u32le(data, record_offset + 0x04)
    payload_size = read_u32le(data, record_offset + 0x08)
    stored_header_crc = read_u16le(data, record_offset + 0x0E)
    computed_header_crc = xmaker_crc16(data[record_offset : record_offset + 0x0E])
    block_count = align_up(payload_size, XMAKER_BLOCK_SIZE) // XMAKER_BLOCK_SIZE
    expected_overhead = align_up(16 + block_count * 2, XMAKER_BLOCK_SIZE)
    payload_offset = record_offset + overhead

    lines.append(f"size={len(data)}")
    lines.append(f"record_offset=0x{record_offset:x}")
    lines.append(f"tag_raw={raw_tag.hex()} tag={decode_highbit_tag(raw_tag)}")
    lines.append(f"record_overhead={overhead} expected={expected_overhead} ok={overhead == expected_overhead}")
    lines.append(f"payload_size={payload_size} block_count={block_count} encrypted={encrypted}")
    lines.append(
        "record_crc="
        f"stored=0x{stored_header_crc:04x} computed=0x{computed_header_crc:04x} "
        f"ok={stored_header_crc == computed_header_crc}"
    )

    if payload_offset + payload_size > len(data):
        lines.append(f"payload_range=0x{payload_offset:x}..0x{payload_offset + payload_size:x} ok=False")
        return lines

    app_info_crc = read_u16le(data, 0x80) if encrypted and len(data) >= 0x82 else 0
    app_key_base = duplicate_u16(app_info_crc) ^ XAPP_KEY
    mismatches: list[str] = []
    for block_index in range(block_count):
        stored = read_u16le(data, record_offset + 0x10 + block_index * 2)
        start = payload_offset + block_index * XMAKER_BLOCK_SIZE
        block = data[start : start + XMAKER_BLOCK_SIZE]
        if encrypted:
            if xor_table is None:
                raise ValueError("encrypted inspection requires the xmaker XOR table")
            block_key = stored ^ app_key_base
            block, _final_key = xmaker_xor_stream(block, block_key, xor_table)
        computed = xmaker_crc16(block, seed=block_index + 1)
        if stored != computed:
            mismatches.append(f"{block_index}:stored=0x{stored:04x},computed=0x{computed:04x}")

    lines.append(f"payload_offset=0x{payload_offset:x}")
    if encrypted:
        lines.append(f"app_info_crc=0x{app_info_crc:04x}")
    lines.append(f"block_crc_ok={not mismatches}")
    if mismatches:
        lines.append("block_crc_mismatches=" + ";".join(mismatches[:8]))

    return lines


def find_tag(data: bytes, tag: str, start: int = 0) -> int:
    raw = highbit_tag_bytes(tag)
    pos = data.find(raw, start)
    if pos < 0:
        raise ValueError(f"tag {tag!r} not found")
    return pos


def build_experimental_dcf(header: bytes, app: bytes, xor_table: list[int]) -> bytes:
    """Build an experimental DCF wrapper for the normal header+app path."""

    body = bytearray(build_experimental_body(header, app, xor_table, encrypt=True))
    body_len = len(body)
    xcod_record = FIRST_BODY_SIZE
    xcod_overhead = read_u32le(body, xcod_record + 0x04)
    xcod_payload_size = read_u32le(body, xcod_record + 0x08)
    app_payload_offset = xcod_record + xcod_overhead
    app_payload_end = app_payload_offset + xcod_payload_size

    dev_len = 0x0C if read_u32le(header, 0x58) > 0 else 0x08
    key_record_len = 0x10 if header[0x5C] != 0 or read_u16le(header, 0x64) != 0 else 0
    app_segment_extra = 0x08
    wrapper_len = 0x48 + dev_len + key_record_len + app_segment_extra

    dcf = bytearray(wrapper_len)
    put_tag(dcf, 0x00, "DCF\0", highbit=False)
    put_u32le(dcf, 0x04, body_len + wrapper_len - 0x08)

    put_tag(dcf, 0x08, "XEAD")
    put_u32le(dcf, 0x0C, wrapper_len - 0x18)

    put_tag(dcf, 0x10, "INFO")
    put_u32le(dcf, 0x14, 0x08)
    put_u32le(dcf, 0x18, read_u32le(body, 0x04))
    put_u32le(dcf, 0x1C, read_u32le(body, 0x08))

    pos = 0x20
    put_tag(dcf, pos, "DEV\0")
    put_u32le(dcf, pos + 0x04, dev_len)
    put_u16le(dcf, pos + 0x08, read_u16le(header, 0x52))
    dcf[pos + 0x0C : pos + 0x10] = header[0x54:0x58]
    if dev_len > 0x08:
        put_u32le(dcf, pos + 0x10, read_u32le(header, 0x58))
    pos = 0x28 + dev_len

    if key_record_len:
        put_tag(dcf, pos, "KEY\0")
        put_u32le(dcf, pos + 0x04, 0x08)
        dcf[pos + 0x08] = header[0x5C]
        put_u16le(dcf, pos + 0x0A, read_u16le(header, 0x64))
        put_u32le(dcf, pos + 0x0C, read_u32le(header, 0x60) ^ SEG_KEY_XOR)
        pos += key_record_len

    put_tag(dcf, pos, "SEG\0")
    put_u32le(dcf, pos + 0x04, 0x0C)
    put_u32le(dcf, pos + 0x08, 0x01)
    put_u32le(dcf, pos + 0x0C, app_payload_offset)
    put_u32le(dcf, pos + 0x10, app_payload_end)
    pos += 0x14

    crc_pos = pos
    put_tag(dcf, crc_pos, "CRC\0")
    put_u32le(dcf, crc_pos + 0x04, 0x04)
    body_crc = xmaker_crc16(bytes(body))
    put_u16le(dcf, crc_pos + 0x08, body_crc)
    wrapper_crc = xmaker_crc16(bytes(dcf[: crc_pos + 0x0A]))
    put_u16le(dcf, crc_pos + 0x0A, wrapper_crc)
    pos += 0x0C

    put_tag(dcf, pos, "DATA")
    put_u32le(dcf, pos + 0x04, body_len)
    pos += 0x08
    if pos != wrapper_len:
        raise ValueError(f"wrapper length mismatch: computed={wrapper_len} written={pos}")

    if header[0x5C] != 0:
        xor_stream_inplace(body, 0, body_len, read_u32le(header, 0x60), xor_table)

    return bytes(dcf + body)


def inspect_experimental_dcf(data: bytes, header: bytes, xor_table: list[int]) -> list[str]:
    lines: list[str] = []
    if len(data) < 0x20:
        raise ValueError("DCF is too small")
    if data[:4] != b"DCF\0":
        raise ValueError("DCF magic not found")

    declared_size = read_u32le(data, 0x04)
    lines.append(f"size={len(data)} declared_payload={declared_size} ok={declared_size == len(data) - 8}")
    lines.append(f"xead_tag={decode_highbit_tag(data[0x08:0x0C])} xead_len={read_u32le(data, 0x0C)}")
    lines.append(f"info_tag={decode_highbit_tag(data[0x10:0x14])} info_len={read_u32le(data, 0x14)}")
    lines.append(f"dev_tag={decode_highbit_tag(data[0x20:0x24])} dev_len={read_u32le(data, 0x24)}")

    crc_pos = find_tag(data, "CRC\0", 0x20)
    data_pos = find_tag(data, "DATA", crc_pos + 0x0C)
    body_len = read_u32le(data, data_pos + 0x04)
    body_offset = data_pos + 0x08
    body = bytearray(data[body_offset : body_offset + body_len])
    if len(body) != body_len:
        lines.append(f"body_range=0x{body_offset:x}..0x{body_offset + body_len:x} ok=False")
        return lines

    if header[0x5C] != 0:
        xor_stream_inplace(body, 0, body_len, read_u32le(header, 0x60), xor_table)

    stored_body_crc = read_u16le(data, crc_pos + 0x08)
    computed_body_crc = xmaker_crc16(bytes(body))
    stored_wrapper_crc = read_u16le(data, crc_pos + 0x0A)
    computed_wrapper_crc = xmaker_crc16(data[: crc_pos + 0x0A])

    lines.append(f"crc_record_offset=0x{crc_pos:x}")
    lines.append(f"data_record_offset=0x{data_pos:x}")
    lines.append(f"body_offset=0x{body_offset:x} body_len={body_len}")
    lines.append(
        "body_crc="
        f"stored=0x{stored_body_crc:04x} computed=0x{computed_body_crc:04x} "
        f"ok={stored_body_crc == computed_body_crc}"
    )
    lines.append(
        "wrapper_crc="
        f"stored=0x{stored_wrapper_crc:04x} computed=0x{computed_wrapper_crc:04x} "
        f"ok={stored_wrapper_crc == computed_wrapper_crc}"
    )
    lines.append(f"final_body_xor={header[0x5C] != 0}")

    return lines


def cmd_xm(args: argparse.Namespace) -> None:
    text = decode_xm(read_file(args.path))
    print(text)


def cmd_tables(args: argparse.Namespace) -> None:
    data = read_file(args.exe)
    crc_table = extract_crc16_table(data)
    xor_table = extract_xor32_table(data)
    generated_crc = make_crc16_table()

    print(f"exe={args.exe}")
    print(f"sha256={hashlib.sha256(data).hexdigest()}")
    print(f"crc16_table_va=0x{CRC16_TABLE_VA:x}")
    print(f"crc16_table_matches_ccitt={crc_table == generated_crc}")
    print("crc16_first16=" + " ".join(f"{value:04x}" for value in crc_table[:16]))
    print(f"xor32_table_va=0x{XOR32_TABLE_VA:x}")
    print("xor32_first8=" + " ".join(f"{value:08x}" for value in xor_table[:8]))


def cmd_crc(args: argparse.Namespace) -> None:
    data = read_file(args.path)
    if args.length is not None:
        data = data[args.offset : args.offset + args.length]
    else:
        data = data[args.offset :]

    table = make_crc16_table()
    print(f"{xmaker_crc16(data, args.seed, table):04x}")


def cmd_xor(args: argparse.Namespace) -> None:
    exe = read_file(args.exe)
    table = extract_xor32_table(exe)
    data = read_file(args.input)
    transformed, final_key = xmaker_xor_stream(data, args.key, table)
    args.output.write_bytes(transformed)
    print(f"wrote={args.output}")
    print(f"final_key=0x{final_key:08x}")


def cmd_make_body(args: argparse.Namespace) -> None:
    exe = read_file(args.exe)
    xor_table = extract_xor32_table(exe)
    header = read_file(args.header)
    app = read_file(args.app)
    body = build_experimental_body(header, app, xor_table, encrypt=args.encrypt)
    args.output.write_bytes(body)
    print(f"wrote={args.output}")
    for line in inspect_experimental_body(body, encrypted=args.encrypt, xor_table=xor_table):
        print(line)


def cmd_inspect_body(args: argparse.Namespace) -> None:
    data = read_file(args.path)
    xor_table = None
    if args.encrypted:
        xor_table = extract_xor32_table(read_file(args.exe))
    for line in inspect_experimental_body(data, args.record_offset, args.encrypted, xor_table):
        print(line)


def cmd_make_dcf(args: argparse.Namespace) -> None:
    exe = read_file(args.exe)
    xor_table = extract_xor32_table(exe)
    header = read_file(args.header)
    app = read_file(args.app)
    dcf = build_experimental_dcf(header, app, xor_table)
    args.output.write_bytes(dcf)
    print(f"wrote={args.output}")
    for line in inspect_experimental_dcf(dcf, header, xor_table):
        print(line)


def cmd_inspect_dcf(args: argparse.Namespace) -> None:
    exe = read_file(args.exe)
    xor_table = extract_xor32_table(exe)
    data = read_file(args.path)
    header = read_file(args.header)
    for line in inspect_experimental_dcf(data, header, xor_table):
        print(line)


def cmd_tags(_args: argparse.Namespace) -> None:
    tags = [
        bytes.fromhex("44434600"),
        bytes.fromhex("58434647"),
        bytes.fromhex("5846494c"),
        bytes.fromhex("58415050"),
        bytes.fromhex("4c564d47"),
        bytes.fromhex("d8c5c1c4"),
        bytes.fromhex("d8d2c5d3"),
        bytes.fromhex("d8c3cfc4"),
        bytes.fromhex("cbc5d900"),
        bytes.fromhex("c9cec6cf"),
        bytes.fromhex("c4c5d600"),
        bytes.fromhex("d3c5c700"),
        bytes.fromhex("c3d2c300"),
        bytes.fromhex("c4c1d4c1"),
        bytes.fromhex("c4d4c100"),
    ]

    for raw in tags:
        print(f"{raw.hex()} {decode_highbit_tag(raw)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)

    xm = sub.add_parser("xm", help="decode an XM script")
    xm.add_argument("path", type=Path)
    xm.set_defaults(func=cmd_xm)

    tables = sub.add_parser("tables", help="extract xmaker tables from the Windows EXE")
    tables.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    tables.set_defaults(func=cmd_tables)

    crc = sub.add_parser("crc16", help="compute the xmaker CRC16 over a file range")
    crc.add_argument("path", type=Path)
    crc.add_argument("--offset", type=lambda value: int(value, 0), default=0)
    crc.add_argument("--length", type=lambda value: int(value, 0))
    crc.add_argument("--seed", type=lambda value: int(value, 0), default=0xFFFF)
    crc.set_defaults(func=cmd_crc)

    xor = sub.add_parser("xor", help="apply the xmaker rolling XOR stream")
    xor.add_argument("input", type=Path)
    xor.add_argument("output", type=Path)
    xor.add_argument("--key", type=lambda value: int(value, 0), required=True)
    xor.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    xor.set_defaults(func=cmd_xor)

    make_body = sub.add_parser(
        "make-body",
        help="build the confirmed inner body portion of make(header, app); not a flashable DCF",
    )
    make_body.add_argument("header", type=Path)
    make_body.add_argument("app", type=Path)
    make_body.add_argument("output", type=Path)
    make_body.add_argument("--encrypt", action="store_true", help="also apply the recovered body/app XOR passes")
    make_body.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    make_body.set_defaults(func=cmd_make_body)

    inspect_body = sub.add_parser("inspect-body", help="inspect an experimental xmaker inner body")
    inspect_body.add_argument("path", type=Path)
    inspect_body.add_argument("--record-offset", type=lambda value: int(value, 0), default=FIRST_BODY_SIZE)
    inspect_body.add_argument("--encrypted", action="store_true", help="decrypt app blocks before checking CRCs")
    inspect_body.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    inspect_body.set_defaults(func=cmd_inspect_body)

    make_dcf = sub.add_parser(
        "make-dcf",
        help="build an experimental DCF for make(header, app); requires vendor comparison before flashing",
    )
    make_dcf.add_argument("header", type=Path)
    make_dcf.add_argument("app", type=Path)
    make_dcf.add_argument("output", type=Path)
    make_dcf.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    make_dcf.set_defaults(func=cmd_make_dcf)

    inspect_dcf = sub.add_parser("inspect-dcf", help="inspect an experimental xmaker DCF")
    inspect_dcf.add_argument("path", type=Path)
    inspect_dcf.add_argument("--header", type=Path, default=Path("tools/header.bin"))
    inspect_dcf.add_argument("--exe", type=Path, default=Path("tools/windows/riscv32-elf-xmaker.exe"))
    inspect_dcf.set_defaults(func=cmd_inspect_dcf)

    tags = sub.add_parser("tags", help="print known xmaker tags")
    tags.set_defaults(func=cmd_tags)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
