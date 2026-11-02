#!/usr/bin/env python3
"""Compare nanos.o machine code and recovered C with identical ROM/MMIO hooks.

This checks the library adapter, not the ROM scheduler or hardware timing.
Requires clang, RV32 ld.lld, Unicorn and pyelftools.
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
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
    UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0)
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_nanos_recovered.c'
API = ('nanos_keep_inside_reach', 'lm_proc_do_on_kiss', 'bb_do_on_ordinary',
       'nanos_save_to_draft', 'bb_find_to_afternoon', 'nanos_write_inside_process')
HOOKS = {0x84390: 'register_timer', 0x8435c: 'register_event',
         0x845b8: 'read_time', 0x84344: 'dispatch',
         0x90000: 'bthw_soft_kick', 0x90010: 'nanos_event_set_trigger'}
REGS = (0x800c, 0x8018, 0x80e0)
TIMER, STOP, STACK = 0x40000, 0xf0000, 0xe0000


def member(archive, name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib' / archive), name])


def build(linker, tmp):
    original = member('libbtctrl.a', 'nanos.o')
    (tmp / 'original.o').write_bytes(original)
    (tmp / 'save.o').write_bytes(member('libhal.a', 'save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac',
                    '-mabi=ilp32', '-ffreestanding', '-fno-builtin', '-Os',
                    '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'bare/drivers/inc'),
                    '-c', str(SOURCE), '-o', str(tmp / 'recovered.o')], check=True)
    for name in ('original', 'recovered'):
        obj = ELFFile(io.BytesIO((tmp / (name + '.o')).read_bytes()))
        symbols = obj.get_section_by_name('.symtab')
        for function in API:
            symbol = symbols.get_symbol_by_name(function)[0]
            assert symbol['st_info']['type'] == 'STT_FUNC'
            assert symbol['st_info']['bind'] == 'STB_GLOBAL'
            if function in API[3:]:
                assert obj.get_section(symbol['st_shndx']).name == '.com_text.bb.nanos'
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for name in ('original', 'recovered'):
        elf = tmp / (name + '.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'bb_do_on_ordinary', '-o', str(elf),
                        str(tmp / (name + '.o')), str(tmp / 'save.o'),
                        '--defsym=bthw_soft_kick=0x90000',
                        '--defsym=nanos_event_set_trigger=0x90010'], check=True)
        binaries.append(elf.read_bytes())
    listing = subprocess.check_output(['llvm-objdump-10', '-dr', str(tmp / 'original.o')], text=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'bt-nanos.llvm.txt').write_text(listing.replace(str(tmp / 'original.o'), 'libbtctrl.a:nanos.o'))
    return binaries, original


class Machine:
    def __init__(self, binary, regs, ticks, timer_value, changing):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in API}
        self.regs, self.trace, self.ticks = dict(regs), [], ticks
        self.changing, self.control_reads = changing, 0
        self.timer_data = b'\xa5' * 8 + struct.pack('<H', timer_value) + b'\x5a' * 6
        self.uc.mem_write(TIMER, self.timer_data)
        for address, name in HOOKS.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0x8000, end=0x80ff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0x8000, end=0x80ff)
        for count in (0, 1):
            address = symbols.get_symbol_by_name(f'__riscv_restore_{count}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=address, end=address)

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        if count:
            uc.reg_write(UC_RISCV_REG_S0, struct.unpack('<I', uc.mem_read(sp, 4))[0])
        ra = struct.unpack('<I', uc.mem_read(sp + count * 4, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + (count + 1) * 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def hook(self, uc, address, size, name):
        a0, a1 = (uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1))
        if name == 'register_timer':
            assert a0 == self.api['nanos_save_to_draft']
            assert a1 == self.api['nanos_write_inside_process']
            self.trace.append(('call', name, 'nanos_save_to_draft', 'nanos_write_inside_process'))
        elif name == 'register_event':
            assert a0 == self.api['bb_find_to_afternoon']
            self.trace.append(('call', name, 'bb_find_to_afternoon'))
        elif name == 'dispatch':
            self.trace.append(('call', name, a0))
        else:
            self.trace.append(('call', name))
        uc.reg_write(UC_RISCV_REG_A0, self.ticks if name == 'read_time' else 0xdeadbeef)
        uc.reg_write(UC_RISCV_REG_A1, 0xa5a5a5a5)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS
        if address == 0x800c:
            self.control_reads += 1
            if self.changing and self.control_reads == 2:
                self.regs[address] ^= 0xdead0001
        self.trace.append(('read', address, self.regs[address]))
        uc.mem_write(address, struct.pack('<I', self.regs[address]))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS
        self.regs[address] = value
        self.trace.append(('write', address, value))

    def call(self, name, value=0):
        self.uc.reg_write(UC_RISCV_REG_SP, STACK)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_S0, 0x12345678)
        self.uc.reg_write(UC_RISCV_REG_A0, value)
        self.uc.emu_start(self.api[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        assert self.uc.reg_read(UC_RISCV_REG_SP) == STACK
        assert self.uc.reg_read(UC_RISCV_REG_S0) == 0x12345678
        if name in (API[0], API[1], API[3]):
            self.trace.append(('return', self.uc.reg_read(UC_RISCV_REG_A0)))

    def snapshot(self):
        return self.trace, self.regs, bytes(self.uc.mem_read(TIMER, 16))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-nanos-') as directory:
        binaries, original = build(args.linker, Path(directory))
    rng = random.Random(0x4e414e4f)
    report = {'original_object_sha256': hashlib.sha256(original).hexdigest(),
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'unicorn_version': unicorn.__version__,
              'model': 'Six nanos.o exports; synthetic MMIO, ROM-call hooks and vendor restore ABI shims. No ROM implementation, silicon timing or RF validation.',
              'cases': []}
    events = [0, 1, 5, 6, 7, 255, 256, 0x10006, 0xffffffff]
    ticks_values = [0, 1, 15, 16, 0xfffff, 0x100000, 0xfffffff0, 0xffffffff]
    for index in range(144):
        regs = {address: rng.getrandbits(32) for address in REGS}
        regs[0x800c] = (regs[0x800c] & ~0x400) | (0x400 if index & 1 else 0)
        ticks = ticks_values[index % len(ticks_values)] if index < 72 else rng.getrandbits(32)
        timer = (0, 1, 0x7fff, 0x8000, 0xffff)[index % 5]
        machines = [Machine(binary, regs, ticks, timer, bool(index & 2)) for binary in binaries]
        actions = [(API[0], 0), (API[1], 0), (API[2], 0), (API[3], 0),
                   (API[4], events[index % len(events)]), (API[5], TIMER),
                   (API[5], TIMER), (API[5], 0), (API[5], TIMER), (API[5], 0)]
        for action in actions:
            for machine in machines:
                machine.call(*action)
            assert machines[0].snapshot() == machines[1].snapshot(), (index, action, machines[0].snapshot(), machines[1].snapshot())
            assert machines[0].snapshot()[2] == machines[0].timer_data
        report['cases'].append({'index': index, 'actions': len(actions), 'result': 'pass'})
    (OUT / 'bt-nanos-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; six nanos.o exports, original vs recovered RV32')


if __name__ == '__main__':
    main()
