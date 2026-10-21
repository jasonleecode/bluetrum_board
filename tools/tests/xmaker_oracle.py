"""Execute the vendor's unmodified x86 packing code with Unicorn (test only).

Entry 0x40ea66 is immediately after XM argument resolution and file loading;
exit 0x4104ac is after the completed output-buffer copy. Buffer objects use the
vendor's 64 KiB linked-list layout, zero-filled as in 0x401000. The test supplies
default script settings (constructor 0x4082f8..0x408327), not header bytes.
Only C runtime allocation, memset and console output are substituted.
"""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xmaker_re
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ESI, UC_X86_REG_EIP


class VendorPacker:
    def __init__(self, exe):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0, 0x1000)  # FS:0 exception registration
        self.uc.mem_map(0x400000, 0x70000)
        for _, va, raw, size in xmaker_re.pe_sections(exe):
            self.uc.mem_write(va, exe[raw:raw + size])
        self.uc.mem_map(0x1000000, 0x8000000)
        self.uc.mem_map(0x30000000, 0x100000)
        self.next = 0x1000000
        self.hooks = {}
        for addr in (0x411cf4, 0x411d2e, 0x421880, 0x41d651, 0x41ff15):
            self.uc.hook_add(UC_HOOK_CODE, self.hook, begin=addr, end=addr)

    def u32(self, addr):
        return struct.unpack('<I', self.uc.mem_read(addr, 4))[0]

    def put(self, addr, value):
        self.uc.mem_write(addr, struct.pack('<I', value))

    def alloc(self, size):
        addr = self.next
        self.next += (size + 15) & ~15
        if self.next >= 0x9000000:
            raise RuntimeError('oracle heap exhausted')
        return addr

    def hook(self, uc, addr, size, _):
        self.hooks[hex(addr)] = self.hooks.get(hex(addr), 0) + 1
        sp = uc.reg_read(UC_X86_REG_ESP)
        result = 0
        if addr in (0x411cf4, 0x411d2e):
            result = self.alloc(self.u32(sp + 4))
        elif addr == 0x421880:
            result = self.u32(sp + 4)
            uc.mem_write(result, bytes([self.u32(sp + 8) & 255]) * self.u32(sp + 12))
        elif addr == 0x41ff15:
            raise RuntimeError(f'vendor exception, return address {self.u32(sp):#x}')
        uc.reg_write(UC_X86_REG_EAX, result)
        uc.reg_write(UC_X86_REG_EIP, self.u32(sp))
        uc.reg_write(UC_X86_REG_ESP, sp + 4)

    def buffer(self, data, logical_size=None):
        obj = self.alloc(32)
        first = previous = 0
        for offset in range(0, len(data), 65536):
            node = self.alloc(24)
            mem = self.alloc(65536)
            self.uc.mem_write(mem, data[offset:offset + 65536])
            self.uc.mem_write(node, struct.pack('<6I', mem, offset, offset, 65536, previous, 0))
            if previous:
                self.put(previous + 20, node)
            else:
                first = node
            previous = node
        self.uc.mem_write(obj, struct.pack('<5I', first, previous, first, 0,
                                         len(data) if logical_size is None else logical_size))
        return obj

    def read_buffer(self, obj):
        size = self.u32(obj + 16)
        out = bytearray(size)
        node = self.u32(obj)
        while node:
            mem, offset = struct.unpack('<2I', self.uc.mem_read(node, 8))
            length = min(65536, size - offset)
            if length > 0:
                out[offset:offset + length] = self.uc.mem_read(mem, length)
            node = self.u32(node + 20)
        return bytes(out)

    def pack(self, header, app):
        body = self.buffer(b'')
        output = self.buffer(b'')
        head = self.buffer(header)
        application = self.buffer(app, (len(app) + 511) // 512 * 512)
        settings = self.alloc(128)
        sp = 0x30080000
        self.put(sp + 0x20, head)
        self.put(sp + 0x48, settings)
        self.put(sp + 0x4c, application)
        for reg, value in ((UC_X86_REG_ESP, sp), (UC_X86_REG_ESI, settings),
                           (UC_X86_REG_EBX, body), (UC_X86_REG_EBP, output)):
            self.uc.reg_write(reg, value)
        self.uc.emu_start(0x40ea66, 0x4104ac, count=20_000_000_000)
        if self.uc.reg_read(UC_X86_REG_EIP) != 0x4104ac:
            raise RuntimeError('vendor execution exceeded instruction limit')
        return self.read_buffer(output), self.read_buffer(body)
