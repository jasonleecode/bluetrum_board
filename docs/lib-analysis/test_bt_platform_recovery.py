#!/usr/bin/env python3
"""Compare original baseband platform RV32 code with its partial C recovery.

Requires clang, ld.lld, Unicorn and pyelftools. IRQ callbacks are invoked as
ordinary functions, as in the SDK dispatcher; no silicon interrupt is emulated.
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
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_platform_recovered.c'
API = ('baseband_platform_pass_by_hello', 'bthw_irq_init', 'bthw_isr_do_plus', 'baseband_platform_get_of_officer')
HOOKS = ('bt_get_packet', 'register_isr', 'bthw_isr_do', 'bthw_thread_post', 'os_interrupt_enter', 'os_interrupt_leave')
ADDRESSES = {name: 0x190000 + i * 16 for i, name in enumerate(HOOKS)}
TARGET, STOP = 0x180000, 0x1f0000
REGS = (0x3a4, 0x3f8, 0x544, 0x548, 0x54c)


def member(archive, name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib' / archive), name])


def build(linker, tmp):
    original, save, recovered = (tmp / name for name in ('original.o', 'save.o', 'recovered.o'))
    original.write_bytes(member('libbtctrl.a', 'baseband_platform.o'))
    save.write_bytes(member('libhal.a', 'save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    for obj in (original, recovered):
        elf_object = ELFFile(io.BytesIO(obj.read_bytes()))
        state = elf_object.get_section_by_name('.symtab').get_symbol_by_name('bb_rf_read_inside_upper')[0]
        section = elf_object.get_section(state['st_shndx'])
        assert state['st_size'] == 8 and state['st_info']['bind'] == 'STB_WEAK'
        assert section.name == '.sbss' and section['sh_addralign'] >= 4
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for obj in (original, recovered):
        elf = obj.with_suffix('.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'bthw_irq_init', '-o', str(elf), str(obj), str(save),
                        f'--defsym=bb_rf_take_without_start={TARGET}',
                        *[f'--defsym={name}={address}' for name, address in ADDRESSES.items()]], check=True)
        binaries.append(elf.read_bytes())
    OUT.mkdir(parents=True, exist_ok=True)
    disassembly = subprocess.check_output(['llvm-objdump-10', '-dr', str(original)], text=True)
    (OUT / 'bt-platform.llvm.txt').write_text(disassembly.replace(str(original), 'libbtctrl.a:baseband_platform.o'))
    return binaries


class Machine:
    def __init__(self, binary, regs, state, packet, changing):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x400000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in API}
        self.state = symbols.get_symbol_by_name('bb_rf_read_inside_upper')[0]['st_value']
        self.uc.mem_write(self.state, state)
        self.regs, self.trace, self.packet, self.changing = dict(regs), [], packet, changing
        self.patch = (regs[0x54c] + 0x1e0) & 0xffffffff
        self.uc.mem_write(self.patch, struct.pack('<I', 0xdeadbeef))
        for name, address in ADDRESSES.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.patch_write, begin=self.patch, end=self.patch + 3)
        for count in (0, 1):
            entry = symbols.get_symbol_by_name(f'__riscv_restore_{count}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=entry, end=entry)

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        if count:
            uc.reg_write(UC_RISCV_REG_S0, struct.unpack('<I', uc.mem_read(sp, 4))[0])
        ra = struct.unpack('<I', uc.mem_read(sp + count * 4, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + (count + 1) * 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def hook(self, uc, address, size, name):
        if name == 'register_isr':
            assert uc.reg_read(UC_RISCV_REG_A1) == self.api['baseband_platform_get_of_officer']
            self.trace.append(('call', name, uc.reg_read(UC_RISCV_REG_A0), 'irq_callback'))
        elif name == 'bt_get_packet':
            self.trace.append(('call', name, uc.reg_read(UC_RISCV_REG_A0), bytes(uc.mem_read(self.state, 8)).hex()))
        else:
            self.trace.append(('call', name))
        # Exercise volatile reads after external calls rather than assuming
        # that bthw_isr_do/os_interrupt_enter cannot alter the control value.
        if self.changing and name in ('bthw_isr_do', 'os_interrupt_enter'):
            self.regs[0x3f8] ^= 0xa5a5a5a5
        uc.reg_write(UC_RISCV_REG_A0, self.packet if name == 'bt_get_packet' else 0xa5a5a5a5)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        self.trace.append(('read', address, self.regs[address]))
        uc.mem_write(address, struct.pack('<I', self.regs[address]))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        self.regs[address] = value & 0xffffffff
        self.trace.append(('write', address, value & 0xffffffff))

    def patch_write(self, uc, access, address, size, value, _):
        assert size == 4 and address == self.patch
        self.trace.append(('vector_write', address, value))

    def call(self, name):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x380000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_S0, 0x12345678)
        self.uc.emu_start(self.api[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, name
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x380000, (name, 'SP')
        assert self.uc.reg_read(UC_RISCV_REG_S0) == 0x12345678, (name, 's0')

    def snapshot(self):
        return self.trace, self.regs, bytes(self.uc.mem_read(self.state, 8)), bytes(self.uc.mem_read(self.patch, 4))


def cases():
    rng = random.Random(0x425450)
    for delta in (0, 1, 2, 0x7fe, 0x800, 0xffe, 0x1000, 0x7fffe, 0xffffe, 0xfffff, 0x100000, -2):
        for packet in (0, 255, 0x12345678, 0xffffffff):
            regs = {reg: rng.getrandbits(32) for reg in REGS}
            regs[0x54c] = TARGET - 0x1e0 - delta
            yield f'init delta {delta} packet {packet:08x}', regs, bytes(rng.randrange(256) for _ in range(8)), packet, False, API[1:2]
    for fill in (0, 1, 2, 3, 0xffffffff, 0xaaaaaaaa, 0x55555555):
        for changing in (False, True):
            regs = dict.fromkeys(REGS, fill)
            regs[0x54c] = TARGET - 0x2000
            yield f'lifecycle fill {fill:08x} changing {changing}', regs, bytes(range(8)), 4, changing, (
                'baseband_platform_pass_by_hello', 'bthw_irq_init', 'baseband_platform_get_of_officer',
                'bthw_isr_do_plus', 'baseband_platform_get_of_officer', 'bthw_isr_do_plus', 'bthw_irq_init')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-platform-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {
        'unicorn_version': unicorn.__version__,
        'original_object_sha256': hashlib.sha256(member('libbtctrl.a', 'baseband_platform.o')).hexdigest(),
        'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'model': 'Synthetic MMIO and platform-call hooks; vendor restore_0/1 ABI shims. Three exports and one registered callback. Vector JAL bytes checked but not executed. No silicon IRQ entry, ROM worker, custom-opcode, cache coherence or RF timing verification.',
        'cases': [],
    }
    for name, regs, state, packet, changing, actions in cases():
        machines = [Machine(binary, regs, state, packet, changing) for binary in binaries]
        for action in actions:
            for machine in machines:
                machine.call(action)
            assert machines[0].snapshot() == machines[1].snapshot(), (name, action, machines[0].snapshot(), machines[1].snapshot())
        report['cases'].append({'name': name, 'api_actions': len(actions), 'trace_events': len(machines[0].trace), 'result': 'pass'})
    (OUT / 'bt-platform-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered platform RV32 code')


if __name__ == '__main__':
    main()
