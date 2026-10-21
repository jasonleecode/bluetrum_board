#!/usr/bin/env python3
"""Differential execution of all six audio.o analog routing/gain functions.

Unicorn runs original RV32 code and recovered C against ordered synthetic
MMIO. This does not validate audio pins, settling, PCM, ADC/DAC DMA or sound.
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
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/audio_recovered.c'
HEADER = ROOT / 'bare/drivers/inc/audio_recovered.h'
STOP = 0x70000
API = ('set_aux_analog_gain', 'set_mic_analog_gain', 'aux_analog_channel_select',
       'aux_analog_channel_close', 'sdadc_analog_mic_start', 'sdadc_analog_mic_exit')
GAIN_TABLE = bytes(range(12)) + bytes(range(18, 30))


def build(linker, tmp):
    objects = {}
    for name in ('audio.o', 'save-restore.o'):
        data = subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])
        (tmp / name).write_bytes(data)
        objects[name] = data
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
                    '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent),
                    '-c', str(SOURCE), '-o', str(tmp / 'recovered.o')], check=True)
    # Explicit section and symbol contracts matter when replacing archive members.
    for name in ('audio.o', 'recovered.o'):
        elf = ELFFile(io.BytesIO((tmp / name).read_bytes()))
        symbols = elf.get_section_by_name('.symtab')
        for api in API:
            s = symbols.get_symbol_by_name(api)[0]
            assert s['st_info']['bind'] == 'STB_GLOBAL'
            assert elf.get_section(s['st_shndx']).name == '.text'
        s = symbols.get_symbol_by_name('mic_anl_table')[0]
        assert s['st_size'] == 24 and s['st_info']['bind'] == 'STB_GLOBAL'
        assert elf.get_section(s['st_shndx']).name == '.rodata'
        assert elf.get_section(s['st_shndx'])['sh_addralign'] >= 4
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.vector*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for name in ('audio.o', 'recovered.o'):
        output = tmp / (name + '.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'set_aux_analog_gain', '-o', str(output), str(tmp / name),
                        str(tmp / 'save-restore.o')], check=True)
        binaries.append(output.read_bytes())
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'audio.llvm.txt').write_text(subprocess.check_output(
        ['llvm-objdump-10', '-dr', 'audio.o'], cwd=tmp, text=True))
    return binaries, objects['audio.o']


class Machine:
    def __init__(self, binary, seed):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for s in elf.iter_sections():
            if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(s['sh_addr'], s.data())
        self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        assert bytes(self.uc.mem_read(self.symbols['mic_anl_table'], 24)) == GAIN_TABLE
        assert self.symbols['mic_anl_table'] % 4 == 0
        rng = random.Random(seed)
        self.regs = {a: rng.getrandbits(32) for a in (0x1e0, 0x1f0, 0x1f4, 0x1f8, 0x1fc)}
        self.trace = []
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        restore = self.symbols['__riscv_restore_1']
        self.uc.hook_add(UC_HOOK_CODE, self.restore, begin=restore, end=restore)

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in self.regs
        value = self.regs[address]
        self.trace.append(('read', address, value))
        uc.mem_write(address, struct.pack('<I', value))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in self.regs
        self.regs[address] = value
        self.trace.append(('write', address, value))

    def restore(self, uc, address, size, _):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        s0, ra = struct.unpack('<II', uc.mem_read(sp, 8))
        uc.reg_write(UC_RISCV_REG_S0, s0)
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + 8)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def call(self, name, *args):
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_S0, 0xa5a55a5a)
        for register, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(register, value)
        self.uc.emu_start(self.symbols[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000
        assert self.uc.reg_read(UC_RISCV_REG_S0) == 0xa5a55a5a


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-audio-') as directory:
        binaries, original = build(args.linker, Path(directory))
    cases = []

    def check(label, actions, seed):
        machines = [Machine(binary, seed) for binary in binaries]
        for machine in machines:
            for action in actions:
                machine.call(*action)
        assert machines[0].trace == machines[1].trace, (label, machines[0].trace, machines[1].trace)
        assert machines[0].regs == machines[1].regs, label
        cases.append({'name': label, 'result': 'pass', 'trace_events': len(machines[0].trace)})

    for seed in (0, 0x12345678):
        for channels in range(256):
            for name in API[2:]:
                action = (name, channels, channels) if name.endswith('_start') else (name, channels)
                check(f'{name} channels {channels:02x} seed {seed}', [action], seed)
        for channels in (0, 1, 0x10, 0x11, 0xcc, 0xffffffff):
            for gain in (*range(32), 255, 256, 0x80000000, 0xffffffff):
                for name in API[:2]:
                    check(f'{name} channels {channels:x} gain {gain} seed {seed}', [(name, gain, channels)], seed)
    for channels in (0, 1, 6, 7, 8, 0x60, 0x70, 0x80, 0x66, 0x77, 0xcc, 0xdd, 0xffffffff):
        check(f'select close lifecycle {channels:x}', [('aux_analog_channel_select', channels),
              ('set_aux_analog_gain', 4, channels), ('aux_analog_channel_close', channels)], 42)
        check(f'mic lifecycle {channels:x}', [('sdadc_analog_mic_start', channels, 17),
              ('set_mic_analog_gain', 3, channels), ('sdadc_analog_mic_exit', channels)], 42)
    report = {
        'unicorn_version': unicorn.__version__,
        'original_object_sha256': hashlib.sha256(original).hexdigest(),
        'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'recovered_header_sha256': hashlib.sha256(HEADER.read_bytes()).hexdigest(),
        'model': 'Synthetic MMIO read/write traces; original save helper executes; custom restore_1 uses ABI shim. No ADC/DAC, analog, PCM, DMA or board validation.',
        'export_contract': 'All six functions GLOBAL .text; mic_anl_table GLOBAL .rodata 24 bytes, identical contents.',
        'cases': cases,
    }
    (OUT / 'audio-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(cases)} audio analog differential cases passed')


if __name__ == '__main__':
    main()
