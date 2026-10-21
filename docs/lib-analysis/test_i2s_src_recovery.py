#!/usr/bin/env python3
"""Compare the recovered SRC-only I2S path with AB53XX libplatform.a:i2s.o.

Requires Unicorn/pyelftools, Clang and RISC-V LLD; no hardware is accessed.
The archive is an external research input, not linked into project firmware.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_SP, UC_RISCV_REG_RA, UC_RISCV_REG_PC

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'bare/drivers/src/i2s_src_recovered.c'
REGS = (0x24, 0x140, 0x4d8, 0x4dc, 0x60c, 0x610, 0x614, 0x68c, 0x690, 0x694)
STOP, STUB, CFG = 0x70000, 0x60000, 0x40000


class Machine:
    def __init__(self, binary, regs):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for s in elf.iter_sections():
            if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(s['sh_addr'], s.data())
        self.sym = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.regs, self.trace = dict(regs), []
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_CODE, self.forbidden, begin=STUB, end=STUB)

    def forbidden(self, *args):
        raise AssertionError('SRC output must not call DMA/IRQ/DAC queue helpers')

    def read(self, uc, access, addr, size, value, user):
        assert size == 4 and addr in REGS, (addr, size)
        value = self.regs[addr]
        self.trace.append(('R', addr, value))
        uc.mem_write(addr, struct.pack('<I', value))

    def write(self, uc, access, addr, size, value, user):
        assert size == 4 and addr in REGS, (addr, size)
        self.regs[addr] = value & 0xffffffff
        self.trace.append(('W', addr, value & 0xffffffff))

    def call(self, name, arg):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_A0, arg)
        self.uc.emu_start(self.sym[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000
        return self.uc.reg_read(UC_RISCV_REG_A0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--linker', required=True)
    args = ap.parse_args()
    original = subprocess.check_output(['ar', 'p', str(args.archive), 'i2s.o'])
    with tempfile.TemporaryDirectory(prefix='ab32-i2s-') as d:
        d = Path(d)
        (d / 'original.o').write_bytes(original)
        subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac',
            '-mabi=ilp32', '-Os', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
            '-Werror', '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE),
            '-o', str(d / 'recovered.o')], check=True)
        (d / 'link.ld').write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text*) } '
            '. = 0x20000; .data : { *(.rodata*) *(.data*) *(.sdata*) } '
            '. = 0x21000; .bss : { *(.bss*) *(.sbss*) } '
            '. = 0x30000; .buffers : { *(.upd_buf*) } }')
        binaries = []
        for name, entry in [('original', 'i2s_init'), ('recovered', 'i2s_src_init')]:
            subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax',
                '-T', str(d / 'link.ld'), '-e', entry, '-o', str(d / (name+'.elf')),
                str(d / (name+'.o')), *[f'--defsym={s}={STUB}' for s in
                ('memset', 'i2s_irq_init', 'i2s_kick_start', 'dac_cb',
                 'obuf_put_one_sample_w', 'obuf_put_samples_w')]], check=True)
            binaries.append((d / (name+'.elf')).read_bytes())
    rng = random.Random(0x493253)
    cases = []
    for index in range(68):
        regs = {r: ([0, 0xffffffff, 0x55555555, 0xaaaaaaaa][index]
                    if index < 4 else rng.getrandbits(32)) for r in REGS}
        for mapping in (0, 1):
            vendor, recovered = [Machine(b, regs) for b in binaries]
            vendor.uc.mem_write(CFG, bytes([0xa8 | mapping]) + bytes(31))
            for repeat in range(2):
                vendor.call('i2s_init', CFG)
                assert recovered.call('i2s_src_init', mapping) == 0
                assert vendor.trace == recovered.trace, (index, mapping, 'init')
                vendor.call('i2s_exit', 0)
                recovered.call('i2s_src_exit', mapping)
                assert vendor.trace == recovered.trace, (index, mapping, 'exit')
                assert vendor.regs == recovered.regs
            cases.append({'state': index, 'mapping': mapping, 'operations': len(vendor.trace), 'passed': True})
    invalid = Machine(binaries[1], dict.fromkeys(REGS, 0))
    for mapping in (2, 255, 0xffffffff):
        assert invalid.call('i2s_src_init', mapping) == 0xffffffff
        invalid.call('i2s_src_exit', mapping)
        assert not invalid.trace
    report = {'original_object_sha256': hashlib.sha256(original).hexdigest(),
        'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'model': 'Ordered synthetic MMIO, SRC/master/16-bit/normal/MCLK256 only. '
                 'No hardware timing or AB32VG1 silicon compatibility proof; DMA/IRQ calls forbidden.',
        'cases': cases, 'invalid_mapping_cases': 3}
    out = ROOT / 'docs/lib-analysis/generated/recovery/i2s-src-differential.json'
    out.write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS: {len(cases)} I2S lifecycle comparisons, 3 invalid mappings; {out}')

if __name__ == '__main__':
    main()
