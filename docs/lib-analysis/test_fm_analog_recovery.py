#!/usr/bin/env python3
"""Execute vendor fmrx_ang.o and recovered C under identical synthetic MMIO.

Needs clang, RV32 ld.lld, unicorn and pyelftools; no board is accessed.
This checks ordered volatile access/delay traces, not RF reception or timing.
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
SOURCE = ROOT / 'bare/drivers/src/fmrx_analog_recovered.c'
DELAY, STOP = 0x60000, 0x70000
API = ('fmrx_set_pll', 'fmrx_analog_init', 'fmrx_set_rf_cap')


def build(linker, tmp):
    objects = {}
    for member in ('fmrx_ang.o', 'save-restore.o'):
        objects[member] = subprocess.check_output(['ar', 'p', str(ROOT/'bare/lib/libhal.a'), member])
        (tmp/member).write_bytes(objects[member])
    disassembly = subprocess.check_output(['llvm-objdump-10', '--mattr=+m', '-dr', 'fmrx_ang.o'], cwd=tmp, text=True)
    (OUT/'fm-analog.llvm.txt').write_text(disassembly)
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(tmp/'recovered.o')], check=True)
    script = tmp/'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for member in ('fmrx_ang.o', 'recovered.o'):
        elf = tmp/(member+'.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'fmrx_analog_init', '-o', str(elf), str(tmp/member),
                        str(tmp/'save-restore.o'), f'--defsym=hal_mdelay={DELAY}'], check=True)
        binaries.append(elf.read_bytes())
    return binaries, objects['fmrx_ang.o']


class Machine:
    def __init__(self, binary, initial, completion):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in API}
        self.uc.mem_write(DELAY, b'\x67\x80\x00\x00')
        self.uc.hook_add(UC_HOOK_CODE, self.delay, begin=DELAY, end=DELAY)
        # Vendor custom-opcode restore is replaced with its established ABI,
        # as in test_spi_recovery.py; body and save helper execute unchanged.
        entry = symbols.get_symbol_by_name('__riscv_restore_3')[0]['st_value']
        self.uc.hook_add(UC_HOOK_CODE, self.restore, begin=entry, end=entry)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        self.regs = dict(initial)
        self.trace = []
        self.completion = completion
        self.pending = None

    def restore(self, uc, address, size, _):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        for register in (UC_RISCV_REG_S2, UC_RISCV_REG_S1, UC_RISCV_REG_S0, UC_RISCV_REG_RA):
            uc.reg_write(register, struct.unpack('<I', uc.mem_read(sp, 4))[0])
            sp += 4
        uc.reg_write(UC_RISCV_REG_SP, sp)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def delay(self, uc, address, size, _):
        self.trace.append(('hal_mdelay', uc.reg_read(UC_RISCV_REG_A0)))
        # Deliberately clobber caller-saved arguments; original must not rely
        # on a particular delay implementation preserving them.
        uc.reg_write(UC_RISCV_REG_A0, 0xdeadbeef)
        uc.reg_write(UC_RISCV_REG_A1, 0xabcdef01)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and 0x410 <= address <= 0x42c
        if address == 0x42c and self.pending is not None:
            self.pending -= 1
            if self.pending == 0:
                self.regs[address] |= 0x80
                self.pending = None
        value = self.regs.get(address, 0)
        self.trace.append(('read', address, value))
        uc.mem_write(address, struct.pack('<I', value))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and 0x410 <= address <= 0x42c
        self.regs[address] = value & 0xffffffff
        self.trace.append(('write', address, value & 0xffffffff))
        if address == 0x42c and value == 1:
            self.pending = self.completion

    def call(self, name, *args, blocked=False):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(reg, value & 0xffffffff)
        self.uc.emu_start(self.api[name], STOP, count=10000)
        returned = self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        assert returned != blocked, (name, 'unexpected return state', returned)
        if blocked:
            assert self.trace[-1] == ('read', 0x42c, 0x2d)
            # Instruction budgets yield different numbers of loop iterations;
            # normalize only repeated terminal wait reads, not earlier RMWs.
            while len(self.trace) > 1 and self.trace[-2] == self.trace[-1]:
                self.trace.pop()


def cases():
    rng = random.Random(0x414)
    initial = [{address: value for address in range(0x410, 0x430, 4)}
               for value in (0, 0xffffffff, 0xa5a55a5a)]
    initial.append({address: rng.getrandbits(32) for address in range(0x410, 0x430, 4)})
    for pattern, registers in enumerate(initial):
        for completion in (2, 5):
            yield f'analog init pattern {pattern} lock {completion}', registers, completion, [('fmrx_analog_init',)], False
        for selector in (0, 1, 0xffffffff):
            for frequency in (0, 6500, 7600, 8750, 9349, 9350, 9351, 10800, 65535, 0xffffffff):
                yield f'PLL pattern {pattern} selector {selector} frequency {frequency}', registers, 4, [('fmrx_set_pll', selector, frequency)], False
        for cap in (0, 1, 15, 31, 32, 255, 0xffffffff):
            yield f'RF cap pattern {pattern} value {cap}', registers, 2, [('fmrx_set_rf_cap', cap)], False
    for index in range(64):
        registers = {address: rng.getrandbits(32) for address in range(0x410, 0x430, 4)}
        actions = [('fmrx_set_pll', rng.getrandbits(32), rng.getrandbits(32))]
        yield f'PLL full-width arithmetic {index}', registers, rng.randrange(2, 7), actions, False
    yield 'initialized retune and cap', initial[0], 3, [
        ('fmrx_analog_init',), ('fmrx_set_pll', 1, 10230), ('fmrx_set_rf_cap', 19),
        ('fmrx_set_pll', 0, 8750), ('fmrx_set_pll', 0, 10800)], False
    yield 'PLL never locks', initial[0], None, [('fmrx_set_pll', 1, 8750)], True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ab32-fm-analog-') as directory:
        binaries, original = build(args.linker, Path(directory))
    report = {
        'unicorn_version': unicorn.__version__,
        'original_object_sha256': hashlib.sha256(original).hexdigest(),
        'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'model': 'Synthetic analog MMIO with configurable PLL-lock status, hal_mdelay recording, __riscv_restore_3 ABI shim; no RF, hardware timing or custom-opcode validation.',
        'abi': {'fmrx_set_pll': ['uint32_t offset_selector', 'uint32_t frequency_10khz'],
                'fmrx_analog_init': [], 'fmrx_set_rf_cap': ['uint32_t cap']},
        'evidence': {'init_pll_call': {'object_offset': '0x296', 'a0': 1, 'a1': 8750},
                     'frequency_threshold': 9350,
                     'pll_formula': 'x = u32(frequency * 10 + selector * 228); u32(x * 2581 + u32(x * 2864) / 26000)',
                     'rf_cap': '0x414 &= ~31; 0x414 |= cap (no input mask)',
                     'timeout': 'Original and recovered code both wait indefinitely for 0x42c bit 7.'},
        'cases': []}
    for name, initial, completion, actions, blocked in cases():
        machines = [Machine(binary, initial, completion) for binary in binaries]
        for action in actions:
            for machine in machines:
                machine.call(*action, blocked=blocked)
            if machines[0].trace != machines[1].trace:
                for index, (left, right) in enumerate(zip(machines[0].trace, machines[1].trace)):
                    if left != right:
                        raise AssertionError((name, action, index, left, right))
                raise AssertionError((name, action, 'trace lengths differ', [len(m.trace) for m in machines]))
            assert machines[0].regs == machines[1].regs, (name, action, 'final register values differ')
        record = {'name': name, 'api_actions': len(actions), 'trace_events': len(machines[0].trace), 'result': 'pass'}
        if name in ('analog init pattern 0 lock 2', 'PLL pattern 0 selector 1 frequency 8750'):
            record['trace'] = machines[0].trace
        report['cases'].append(record)
        print('PASS', name)
    (OUT/'fm-analog-differential.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered RISC-V code')


if __name__ == '__main__':
    main()
