"""Default DCF flashing order recovered from Downloader RIDs 1452, 1463-1467.

The caller supplies a connected bootloader with its RAM loader and Flash context
initialized. No serial port is opened here. The first block is committed only
after verification of the remainder, as in the Windows downloader.
"""

from dataclasses import dataclass
import struct

from bluetrum_dcf import DcfImage
from bluetrum_uart import crc16


BLOCK_SIZE = 512
RESERVED_FLASH_BYTES = 20_480


class VerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class FlashPlan:
    body_bytes: int
    erase_bytes: int
    minimum_flash_bytes: int
    sectors: tuple[tuple[int, int], ...]


def plan_flash(image: DcfImage) -> FlashPlan:
    length = len(image.body)
    if not length or length % BLOCK_SIZE:
        raise ValueError("Flash body must contain whole 512-byte blocks")
    previous = 0
    for start, end in image.segments:
        if start % BLOCK_SIZE or end % BLOCK_SIZE or not previous <= start < end <= length:
            raise ValueError("invalid or overlapping application segments")
        previous = end
    erase_end = (length + 4095) // 4096 * 4096
    sectors = []
    address = 0
    while address < erase_end:
        size = 65536 if address % 65536 == 0 and address + 65536 <= erase_end else 4096
        sectors.append((address, size))
        address += size
    return FlashPlan(length, erase_end, erase_end + RESERVED_FLASH_BYTES, tuple(sectors))


def _selector(image: DcfImage, address: int) -> int:
    return int(any(start <= address and address + BLOCK_SIZE <= end for start, end in image.segments))


def _verify(boot, image: DcfImage, start: int, end: int, progress) -> None:
    boundaries = sorted({start, end, *(point for segment in image.segments for point in segment
                                      if start < point < end)})
    for left, right in zip(boundaries, boundaries[1:]):
        for address in range(left, right, 65536):
            length = min(65536, right - address)
            count = length // BLOCK_SIZE
            response = boot.request(30, value=address, selector=_selector(image, address),
                                    count=count, read_size=count * 2)
            if len(response) != count * 2:
                raise VerificationError(f"short CRC response at Flash address 0x{address:x}")
            for block in range(count):
                offset = address + block * BLOCK_SIZE
                actual, = struct.unpack_from("<H", response, block * 2)
                expected = crc16(image.body[offset:offset + BLOCK_SIZE])
                if actual != expected:
                    raise VerificationError(f"Flash CRC mismatch at 0x{offset:x}: "
                                            f"expected {expected:04x}, got {actual:04x}")
            progress("verify", address + length, len(image.body))


def flash_image(boot, image: DcfImage, flash_size: int, *, progress=None, reboot=True) -> FlashPlan:
    """Erase/write/verify a prepared device; stop before commit on any failure."""
    plan = plan_flash(image)
    if image.chip != boot.info.chip:
        raise ValueError(f"DCF chip {image.chip} does not match device {boot.info.chip}")
    if plan.minimum_flash_bytes > flash_size:
        raise ValueError(f"image requires {plan.minimum_flash_bytes} Flash bytes including "
                         f"20 KiB reserved space; device has {flash_size}")
    if boot.context.operation_mask & 14 != 14:
        raise ValueError("device has not granted Flash write, erase and CRC operations")
    if progress is None:
        progress = lambda *_: None

    for address, size in plan.sectors:
        boot.request(28, value=address, selector=3 if size == 4096 else 1)
        if not boot.ping():
            raise RuntimeError(f"device did not acknowledge erase completion at 0x{address:x}")
        progress("erase", address + size, plan.erase_bytes)

    for address in range(BLOCK_SIZE, len(image.body), BLOCK_SIZE):
        boot.request(27, value=address, selector=_selector(image, address), count=1,
                     write_data=image.body[address:address + BLOCK_SIZE])
        progress("write", address + BLOCK_SIZE, len(image.body))
    _verify(boot, image, BLOCK_SIZE, len(image.body), progress)

    boot.request(27, value=0, selector=0, count=1, write_data=image.body[:BLOCK_SIZE])
    # RID1465 -> RID1464 rechecks the whole prefix up to the first SEG boundary
    # or the 64 KiB query limit, despite passing a loop end of 512 to RID1464.
    prefix_end = min([65536, len(image.body)] + [start for start, _ in image.segments if start > 0])
    _verify(boot, image, 0, prefix_end, progress)
    boot.refresh_key()
    progress("commit", len(image.body), len(image.body))
    if reboot:
        boot.request(22)
    return plan
