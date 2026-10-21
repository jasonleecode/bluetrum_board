#!/usr/bin/env python3
"""Differential execution of usb_device_ep.o and the recovered RV32 C.

Synthetic MMIO verifies register order, transfer state and return values; it
cannot establish hardware enumeration, actual DMA contents or bus timing.
"""
import argparse
import hashlib
import io
import json
import random
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/usb_ep_recovered.c'
HEADER = ROOT / 'bare/drivers/inc/usb_ep_recovered.h'
API = ('usb_set_cur_ep', 'usb_ep_init', 'usb_ep_halt', 'usb_ep_clear', 'usb_ep_transfer', 'usb_ep_reset', 'usb_ep_start_transfer', 'usb_ep_do_transfer')
EP, OLD, DELAY, STOP, BASE = 0x50000, 0x50100, 0x60000, 0x70000, 0x1000
REGS = (0x68, 0x308, 0x318, 0x31c, 0x320, 0x324, 0x328, 0x32c, 0x538, 0x53c, *range(BASE + 0x38, BASE + 0x60, 4))


def member(name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])


def build(linker, tmp):
    original, save, recovered = (tmp / name for name in ('original.o', 'save.o', 'recovered.o'))
    original.write_bytes(member('usb_device_ep.o'))
    save.write_bytes(member('save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32', '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.usbdev.com) *(.vector) } . = 0x20000; .rodata : { *(.rodata*) } . = 0x30000; .data : { *(.data*) *(.sdata*) } .bss : { *(.bss*) *(.sbss*) *(COMMON) } __global_pointer$ = 0x30800; }')
    binaries = []
    for obj in (original, recovered):
        elf = obj.with_suffix('.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script), '-e', 'usb_ep_transfer', '-o', str(elf), str(obj), str(save), f'--defsym=delay_us={DELAY}'], check=True)
        binaries.append(elf.read_bytes())
    OUT.mkdir(parents=True, exist_ok=True)
    disassembly = subprocess.check_output(['llvm-objdump-10', '-dr', str(original)], text=True)
    (OUT / 'usb-ep.llvm.txt').write_text(disassembly.replace(str(original), 'libhal.a:usb_device_ep.o'))
    return binaries


def state(number=1, flags=0, kind=2, remaining=128, transferred=0, packet=64, cursor=0x80000):
    return struct.pack('<IBBBBIIHHHH', BASE, kind, number, flags, 0xab, 0x80000, cursor, remaining, transferred, packet, 0xcdef)


class Machine:
    def __init__(self, binary, initial, registers, schedules, mutate, abort):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in API}
        self.eps = symbols.get_symbol_by_name('eps')[0]['st_value']
        self.uc.mem_write(self.eps, struct.pack('<9I', *([OLD] * 9)))
        self.uc.mem_write(EP, initial)
        self.uc.mem_write(OLD, state(number=0, flags=2))
        self.uc.mem_write(0x80000, bytes(range(256)))
        self.uc.mem_write(DELAY, b'\x67\x80\x00\x00')
        self.regs, self.trace = dict(registers), []
        self.schedules = {address: list(values) for address, values in schedules.items()}
        self.mutate, self.abort = mutate, abort
        self.uc.hook_add(UC_HOOK_CODE, self.delay, begin=DELAY, end=DELAY)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0x1fff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0x1fff)
        for count in range(4):
            entry = symbols.get_symbol_by_name(f'__riscv_restore_{count}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=entry, end=entry)

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)[:count]):
            uc.reg_write(reg, struct.unpack('<I', uc.mem_read(sp + (count - i - 1) * 4, 4))[0])
        ra = struct.unpack('<I', uc.mem_read(sp + count * 4, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + (count + 1) * 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def delay(self, uc, address, size, _):
        self.trace.append(('delay_us', uc.reg_read(UC_RISCV_REG_A0)))
        if self.mutate:
            uc.mem_write(EP + 12, struct.pack('<IHH', 0xfffffff0, 0xfffe, 0xffff))
        uc.reg_write(UC_RISCV_REG_A0, 0xa5a5a5a5)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert address in REGS and size == 4, (address, size)
        values = self.schedules.get(address, [])
        if values:
            self.regs[address] = values.pop(0)
        self.trace.append(('read', address, self.regs[address]))
        uc.mem_write(address, struct.pack('<I', self.regs[address]))

    def write(self, uc, access, address, size, value, _):
        assert address in REGS and size == 4, (address, size)
        self.regs[address] = value & 0xffffffff
        self.trace.append(('write', address, value & 0xffffffff))
        if address == 0x68 and self.abort:
            uc.mem_write(EP + 6, bytes([uc.mem_read(EP + 6, 1)[0] | 1]))

    def call(self, name, length=0, expect_pending=False):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_A0, EP)
        self.uc.reg_write(UC_RISCV_REG_A1, length)
        saved = (UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)
        for i, reg in enumerate(saved):
            self.uc.reg_write(reg, 0x12345678 + i)
        self.uc.emu_start(self.api[name], STOP, count=40000)
        if expect_pending:
            assert self.uc.reg_read(UC_RISCV_REG_PC) != STOP, (name, 'unexpected return')
            return None
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000, (name, 'stack imbalance')
        for i, reg in enumerate(saved):
            assert self.uc.reg_read(reg) == 0x12345678 + i, (name, 'callee-save mismatch')
        assert bytes(self.uc.mem_read(0x80000, 256)) == bytes(range(256)), 'unexpected CPU buffer copy'
        if name in ('usb_ep_transfer', 'usb_ep_start_transfer', 'usb_set_cur_ep'):
            return self.uc.reg_read(UC_RISCV_REG_A0)
        return None

    def snapshot(self):
        return self.trace, self.regs, bytes(self.uc.mem_read(EP, 24)), bytes(self.uc.mem_read(self.eps, 36))


def cases():
    rng = random.Random(0x555342)
    def case(name, initial, actions, regs=None, schedules=None, mutate=False, abort=False):
        registers = {reg: rng.getrandbits(32) for reg in REGS}
        registers[BASE + 0x44] &= ~1
        if regs:
            registers.update(regs)
        return name, initial, registers, schedules or {}, mutate, abort, actions
    for number in range(4):
        for flags in (0, 2, 4, 6, 9, 11, 0xff):
            for kind in (0, 1, 2, 3, 255):
                yield case(f'lifecycle ep {number} flags {flags} type {kind}', state(number, flags, kind), [('usb_set_cur_ep',), ('usb_ep_init',), ('usb_ep_reset',), ('usb_ep_halt',), ('usb_ep_halt',), ('usb_ep_clear',), ('usb_ep_clear',)])
    for number in range(4):
        for length in (0, 1, 63, 64, 65, 128, 65535):
            for busy in (0, 1):
                yield case(f'IN ep {number} length {length} busy {busy}', state(number, 2, transferred=65535), [('usb_ep_start_transfer', length), ('usb_ep_transfer',)], schedules={BASE + 0x44: [busy, 0]})
    for packet in (0, 1, 8, 64, 1024, 65535):
        for length in (0, 1, 64, 128, 65535):
            for received in sorted({0, 1, packet, min(65535, packet + 1)}):
                yield case(f'OUT packet {packet} length {length} received {received}', state(flags=0, remaining=length, packet=packet, transferred=65535), [('usb_ep_transfer',)], regs={BASE + 0x50: 1, BASE + 0x58: received & 255, BASE + 0x5c: received >> 8})
    for flags in (0, 2):
        yield case(f'no ready direction {flags}', state(flags=flags), [('usb_ep_transfer',)], regs={BASE + (0x44 if flags else 0x50): 1 if flags else 0})
    for number in range(4):
        yield case(f'OUT start ep {number}', state(number=number), [('usb_ep_start_transfer', 128), ('usb_ep_transfer',), ('usb_ep_transfer',)], schedules={BASE + 0x50: [1, 1], BASE + 0x58: [64, 64], BASE + 0x5c: [0, 0]})
        yield case(f'IN delay mutation ep {number}', state(number=number, flags=2), [('usb_ep_start_transfer', 128)], mutate=True)
        yield case(f'halt busy changing ep {number}', state(number=number, flags=2), [('usb_ep_halt',)], schedules={BASE + 0x44: [1, 3, 0]})
    for number in (4, 15, 16, 31, 255):
        yield case(f'raw invalid IN endpoint {number}', state(number=number, flags=2), [('usb_ep_start_transfer', 64)])
    for high, low in ((0x100, 0), (0xffffffff, 0xffffffff), (1, 0x12345)):
        yield case(f'OUT unmasked count {high:x} {low:x}', state(), [('usb_ep_transfer',)], regs={BASE + 0x50: 1, BASE + 0x58: low, BASE + 0x5c: high})
    for flags in (0, 1, 2, 3):
        yield case(f'blocking abort flags {flags}', state(flags=flags), [('usb_ep_do_transfer', 128)], abort=True)
    yield case('blocking IN completion', state(flags=2), [('usb_ep_do_transfer', 128)], schedules={BASE + 0x44: [0, 1, 0, 1, 0]})
    yield case('blocking OUT completion', state(), [('usb_ep_do_transfer', 128)], schedules={BASE + 0x50: [0, 1, 1], BASE + 0x58: [64, 64], BASE + 0x5c: [0, 0]})
    for i in range(64):
        initial = state(number=rng.randrange(4), flags=rng.randrange(16), kind=rng.randrange(256), remaining=rng.randrange(65536), transferred=rng.randrange(65536), packet=rng.randrange(65536), cursor=rng.getrandbits(32))
        yield case(f'random transfer {i}', initial, [('usb_ep_transfer',)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-usb-ep-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {'unicorn_version': unicorn.__version__, 'original_object_sha256': hashlib.sha256(member('usb_device_ep.o')).hexdigest(), 'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(), 'header_sha256': hashlib.sha256(HEADER.read_bytes()).hexdigest(), 'model': 'Synthetic indexed USB register values, delay hook and restore_0..3 ABI shims; ordered MMIO, endpoint RAM, eps table, callee-save registers and returns compared. No silicon bus, DMA content or timing verification; do_transfer abort return treated as void.', 'cases': []}
    for name, initial, regs, schedules, mutate, abort, actions in cases():
        machines = [Machine(binary, initial, regs, schedules, mutate, abort) for binary in binaries]
        for action in actions:
            results = [machine.call(*action) for machine in machines]
            assert results[0] == results[1], (name, action, 'return mismatch', results)
            assert machines[0].snapshot() == machines[1].snapshot(), (name, action, machines[0].snapshot(), machines[1].snapshot())
        report['cases'].append({'name': name, 'api_actions': len(actions), 'trace_events': len(machines[0].trace), 'result': 'pass'})
    # Vendor polling deliberately has no timeout. Compare a common MMIO trace
    # prefix and state after a finite instruction budget, not the number of
    # loop iterations (compiler instruction counts differ).
    for name, initial, action, override in (
        ('unbounded halt IN busy', state(flags=2), ('usb_ep_halt', 0), {BASE + 0x44: 1}),
        ('unbounded blocking IN busy', state(flags=2), ('usb_ep_do_transfer', 64), {BASE + 0x44: 1}),
        ('unbounded blocking OUT idle', state(), ('usb_ep_do_transfer', 64), {BASE + 0x50: 0}),
    ):
        regs = dict.fromkeys(REGS, 0)
        regs.update(override)
        machines = [Machine(binary, initial, regs, {}, False, False) for binary in binaries]
        for machine in machines:
            machine.call(*action, expect_pending=True)
        assert machines[0].trace[:16] == machines[1].trace[:16], name
        assert machines[0].snapshot()[1:] == machines[1].snapshot()[1:], name
        report['cases'].append({'name': name, 'api_actions': 1, 'trace_prefix_events': 16, 'result': 'both remain pending after 40000 instructions'})
    (OUT / 'usb-ep-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered endpoint RV32 code')


if __name__ == '__main__':
    main()
