"""Bounded parser for the default, single-application xmaker DCF format.

This checks file integrity and the supported container shape, not whether an
application is suitable for a particular board. Offsets in SEG are body-relative.
"""

from dataclasses import dataclass
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from xmaker_re import MAX_DCF_BODY_SIZE, highbit_tag_bytes, xmaker_crc16


@dataclass(frozen=True)
class DcfImage:
    chip: str
    version: int
    body: bytes
    segments: tuple[tuple[int, int], ...]


def parse_dcf(data: bytes) -> DcfImage:
    """Accept the verified default layout; reject custom KEY/DEV configurations."""
    if len(data) < 88 or data[:4] != b"DCF\0":
        raise ValueError("not a supported DCF container")
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    if u32(4) != len(data) - 8:
        raise ValueError("DCF file length mismatch")
    expected = ((8, "XEAD", 64), (16, "INFO", 8), (32, "DEV\0", 8),
                (48, "SEG\0", 12), (68, "CRC\0", 4))
    for offset, tag, length in expected:
        if data[offset:offset + 4] != highbit_tag_bytes(tag) or u32(offset + 4) != length:
            raise ValueError(f"unsupported or corrupt {tag.rstrip(chr(0))} record")
    if data[80:84] != highbit_tag_bytes("DATA") or u32(84) != len(data) - 88:
        raise ValueError("DCF DATA length mismatch")
    body_crc, wrapper_crc = struct.unpack_from("<HH", data, 76)
    if xmaker_crc16(data[:78]) != wrapper_crc:
        raise ValueError("DCF header CRC mismatch")
    body = data[88:]
    if not body or len(body) > MAX_DCF_BODY_SIZE:
        raise ValueError("DCF body outside downloader size limits")
    if xmaker_crc16(body) != body_crc:
        raise ValueError("DCF body CRC mismatch")
    if data[40:48] != bytes(8):
        raise ValueError("custom DEV settings are not supported")
    if u32(56) != 1:
        raise ValueError("only single-application SEG records are supported")
    start, end = u32(60), u32(64)
    if not 0 <= start < end <= len(body):
        raise ValueError("DCF application segment outside body")
    if len(body) < 0x2400 or body[0x2000:0x2004] != highbit_tag_bytes("XCOD"):
        raise ValueError("missing default XCOD application record")
    overhead, payload_size = struct.unpack_from("<II", body, 0x2004)
    expected_overhead = ((16 + (payload_size // 512) * 2 + 511) // 512) * 512
    if (payload_size == 0 or payload_size % 512 or overhead != expected_overhead
            or start != 0x2000 + overhead or end != start + payload_size or end != len(body)):
        raise ValueError("DCF segment does not match default XCOD layout")
    if (body[0x200c:0x200e] != b"\x10\0"
            or xmaker_crc16(body[0x2000:0x200e]) != struct.unpack_from("<H", body, 0x200e)[0]):
        raise ValueError("DCF XCOD header mismatch")
    chip_bytes = data[24:28]
    if any(byte < 0x21 or byte > 0x7e for byte in chip_bytes):
        raise ValueError("invalid chip tag")
    return DcfImage(chip_bytes.decode("ascii"), u32(28), body, ((start, end),))
