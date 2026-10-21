#!/usr/bin/env python3
"""Compare USB common/detection code with the original RV32 archive members.

Synthetic registers, memset/device-ISR hooks and restore_0 ABI shim; no board.
Requires clang, RISC-V ld.lld, Unicorn and pyelftools.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile

import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/usb_comm_recovered.c'
EPS, MEMSET, ISR, STOP = 0x40000, 0x60000, 0x60010, 0x70000
API = ('usb_disable', 'usb_init', 'usb_interrupt_disable', 'usb_isr',
       'usbchk_only_device', 'usbchk_only_host', 'usbchk_switch_otg_device',
       'usbchk_switch_otg_host', 'usbchk_switch_init_state', 'usbchk_connect')


def member(name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])


def build(linker, tmp):
    inputs = []
    for name in ('usb_comm.o', 'usb_detect.o', 'save-restore.o'):
        path = tmp / name
        path.write_bytes(member(name))
        inputs.append(path)
    recovered = tmp / 'recovered.o'
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) *(.buf*) } '
                      '.bss : { *(.bss*) *(.sbss*) *(COMMON) } __global_pointer$ = 0x30800; }')
    binaries = []
    for name, objs in [('original', inputs), ('recovered', [recovered, inputs[-1]])]:
        elf = tmp / (name + '.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'usb_init', '-o', str(elf), *map(str, objs),
                        f'--defsym=eps={EPS}', f'--defsym=memset={MEMSET}', f'--defsym=usb_dev_isr={ISR}'], check=True)
        binaries.append(elf.read_bytes())
    disassembly = subprocess.check_output(['llvm-objdump-10', '-dr', *map(str, inputs[:2])], text=True)
    for path in inputs[:2]:
        disassembly = disassembly.replace(str(path), 'libhal.a:' + path.name)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'usb-comm.llvm.txt').write_text(disassembly)
    return binaries


class Machine:
    def __init__(self, binary, seed, state):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.symbols = {s.name: s['st_value'] for s in symbols.iter_symbols()}
        assert bytes(self.uc.mem_read(self.symbols['epsize_tbl'], 4)) == bytes([1, 1, 4, 16])
        assert bytes(self.uc.mem_read(self.symbols['ep0_buf'], 64)) == bytes(64)
        for name, size in [('ep0_buf', 64), ('epsize_tbl', 4), ('usbchk_cb', 2)]:
            sym = symbols.get_symbol_by_name(name)[0]
            assert sym['st_size'] == size and sym['st_value'] % 4 == 0
        rng = random.Random(seed)
        self.registers = {a: rng.getrandbits(32) for a in range(0, 0x1000, 4)}
        self.uc.mem_write(self.symbols['usbchk_cb'], bytes(state))
        self.uc.mem_write(EPS, bytes(range(36)))
        self.trace = []
        for address in (MEMSET, ISR):
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.external, begin=address, end=address)
        restore = self.symbols['__riscv_restore_0']
        self.uc.hook_add(UC_HOOK_CODE, self.restore, begin=restore, end=restore)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)

    def external(self, uc, address, size, _):
        if address == MEMSET:
            a, b, c = [uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2)]
            assert a == EPS and b == 0 and c == 36
            self.trace.append(('memset', a, b, c))
            uc.mem_write(a, bytes([b]) * c)
        else:
            self.trace.append(('usb_dev_isr',))
            # Exercise an external call mutating the register model.
            self.registers[0x30c] ^= 0x100
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def restore(self, uc, address, size, _):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        ra = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address % 4 == 0
        self.trace.append(('read', address, self.registers[address]))
        uc.mem_write(address, struct.pack('<I', self.registers[address]))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address % 4 == 0
        self.trace.append(('write', address, value))
        self.registers[address] = value

    def call(self, name, argument=0):
        self.uc.reg_write(UC_RISCV_REG_A0, argument)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.emu_start(self.symbols[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000
        return self.uc.reg_read(UC_RISCV_REG_A0)

    def snapshot(self):
        return (self.trace, self.registers, bytes(self.uc.mem_read(EPS, 36)),
                bytes(self.uc.mem_read(self.symbols['usbchk_cb'], 2)),
                bytes(self.uc.mem_read(self.symbols['ep0_buf'], 64)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-usb-comm-') as directory:
        binaries = build(args.linker, Path(directory))
    cases = []

    def check(label, actions, seed=0, state=(0, 255), overrides=None):
        machines = [Machine(binary, seed, state) for binary in binaries]
        results = []
        for machine in machines:
            machine.registers.update(overrides or {})
            returned = []
            for name, arg in actions:
                value = machine.call(name, arg)
                if name == 'usbchk_connect':
                    returned.append(value)
            results.append(returned)
        assert machines[0].snapshot() == machines[1].snapshot(), label
        assert results[0] == results[1], label
        cases.append({'name': label, 'result': 'pass', 'trace_events': len(machines[0].trace)})

    for seed in range(8):
        for name in API:
            check(f'{name} seed {seed}', [(name, 0x200 if name == 'usb_interrupt_disable' else 0)], seed)
        check(f'lifecycle {seed}', [('usb_init', 0), ('usb_isr', 0), ('usbchk_only_host', 0),
                                  ('usbchk_connect', 1), ('usb_interrupt_disable', 0x200), ('usb_disable', 0)], seed)
    for state in (0, 1, 2, 3, 255):
        for mode in (0, 1, 2, 3, 255, 0xffffffff):
            for status in (0, 0x100, 0xffffffff):
                check(f'connect state {state} mode {mode} status {status:x}', [('usbchk_connect', mode)],
                      state=(state, 0xa5), overrides={0x304: status})
    for base in (0x200, 0x800):
        check(f'interrupt register base {base:x}', [('usb_interrupt_disable', base)])
    for status in (0, 1, 2, 3, 0xffffffff):
        check(f'IRQ status {status:x}', [('usb_isr', 0)], overrides={0x30c: status})
    report = {'unicorn_version': unicorn.__version__,
              'original_object_sha256': {name: hashlib.sha256(member(name)).hexdigest()
                                         for name in ('usb_comm.o', 'usb_detect.o')},
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'model': 'Synthetic MMIO; memset/device ISR hooks; restore_0 ABI shim. No USB PHY, host, IRQ or wire timing validation.',
              'cases': cases}
    (OUT / 'usb-comm-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(cases)} USB common/detection differential cases passed')


if __name__ == '__main__':
    main()
