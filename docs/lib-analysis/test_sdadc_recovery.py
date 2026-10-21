#!/usr/bin/env python3
"""RV32 original/recovered SDADC differential tests; synthetic MMIO, no ADC/DMA hardware."""
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
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/sdadc_recovered.c'
HOOKS = {name: 0x60000 + i * 16 for i, name in enumerate(('hal_printf', 'hal_mdelay', 'sdadc_analog_mic_start', 'sdadc_analog_aux_start', 'sdadc_analog_mic_exit', 'sdadc_analog_aux_exit', 'sdadc_dma_notice'))}
STOP = 0x70000
GETTERS = ('sdadc_get_channel', 'sdadc_get_sample_rate', 'sdadc_get_gain', 'sdadc_get_dma_samples')


def member(name):
    return subprocess.check_output(['ar', 'p', ROOT / 'bare/lib/libhal.a', name])


def build(linker, tmp):
    original, save, recovered = [tmp / n for n in ('sdadc.o', 'save.o', 'recovered.o')]
    original.write_bytes(member('sdadc.o'))
    save.write_bytes(member('save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32', '-Os', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-Wall', '-Wextra', '-Werror', '-I', ROOT / 'bare/drivers/inc', '-c', SOURCE, '-o', recovered], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.irq*) } . = 0x20000; .rodata : { *(.rodata*) *(.srodata*) } . = 0x30000; .data : { *(.data*) *(.sdata*) } .bss : { *(.bss*) *(.sbss*) *(COMMON) } __global_pointer$ = 0x30800; }')
    binaries = []
    for name, obj in [('original', original), ('recovered', recovered)]:
        elf = tmp / (name + '.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', script, '-e', 'sdadc_start', '-o', elf, obj, save, *[f'--defsym={n}={a}' for n, a in HOOKS.items()]], check=True)
        binaries.append(elf.read_bytes())
    dis = subprocess.check_output(['llvm-objdump-10', '-dr', original], text=True).replace(str(original), 'libhal.a:sdadc.o')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'sdadc.llvm.txt').write_text(dis)
    return binaries


class Machine:
    def __init__(self, binary, seed, status=0):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for sec in elf.iter_sections():
            if sec['sh_flags'] & 2 and sec['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(sec['sh_addr'], sec.data())
        syms = elf.get_section_by_name('.symtab')
        self.symbols = {s.name: s['st_value'] for s in syms.iter_symbols()}
        rng = random.Random(seed)
        self.regs = {a: rng.getrandbits(32) for a in range(0, 0x1000, 4)}
        self.regs[0x1c8] = status
        self.trace = []
        for name, address in HOOKS.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.external, name, begin=address, end=address)
        for count in range(4):
            address = self.symbols[f'__riscv_restore_{count}']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=address, end=address)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)

    def external(self, uc, address, size, name):
        a, b = uc.reg_read(UC_RISCV_REG_A0), uc.reg_read(UC_RISCV_REG_A1)
        if name == 'hal_printf':
            fmt = bytes(uc.mem_read(a, 64)).split(b'\0')[0]
            self.trace.append((name, fmt.decode(), b))
        elif name.endswith('_start'):
            self.trace.append((name, a, b))
        else:
            self.trace.append((name, a))
        uc.reg_write(UC_RISCV_REG_A0, 0xa5a5a5a5)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)[:count]):
            uc.reg_write(reg, struct.unpack('<I', uc.mem_read(sp + (count-i-1)*4, 4))[0])
        ra = struct.unpack('<I', uc.mem_read(sp + count*4, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + (count+1)*4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address % 4 == 0
        self.trace.append(('read', address, self.regs[address]))
        uc.mem_write(address, struct.pack('<I', self.regs[address]))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address % 4 == 0
        self.trace.append(('write', address, value))
        self.regs[address] = value
        if address == 0x1c4:
            self.regs[0x1c8] &= ~value

    def call(self, name, arg=0):
        uc = self.uc
        uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        uc.reg_write(UC_RISCV_REG_RA, STOP)
        uc.reg_write(UC_RISCV_REG_A0, arg)
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)):
            uc.reg_write(reg, 0x12345678+i)
        uc.emu_start(self.symbols[name], STOP, count=20000)
        assert uc.reg_read(UC_RISCV_REG_PC) == STOP, name
        assert uc.reg_read(UC_RISCV_REG_SP) == 0x90000, name
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)):
            assert uc.reg_read(reg) == 0x12345678+i, (name, 'callee save')
        return None if name == 'sdadc_isr' else uc.reg_read(UC_RISCV_REG_A0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker: parser.error('provide --linker')
    with tempfile.TemporaryDirectory(prefix='ab32-sdadc-') as tmp:
        binaries = build(args.linker, Path(tmp))
    report = {'unicorn_version': unicorn.__version__, 'original_object_sha256': hashlib.sha256(member('sdadc.o')).hexdigest(), 'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(), 'model': 'Synthetic MMIO and analog/delay/notice hooks; restore ABI shim; original vs recovered ordered accesses, calls, getters, returns and callee-save registers. No DMA extent/units, samples, PLL timing or real IRQ verification.', 'cases': []}
    def check(label, actions, seed=0, status=0):
        machines = [Machine(binary, seed, status) for binary in binaries]
        snapshots = []
        for m in machines:
            returned=[]
            for name,arg in actions:
                returned.append(m.call(name,arg))
                returned.extend(m.call(g) for g in GETTERS)
            snapshots.append((m.trace, m.regs, returned))
        if snapshots[0] != snapshots[1]:
            for i,(a,b) in enumerate(zip(snapshots[0][0], snapshots[1][0])):
                if a!=b: print('trace difference',i,a,b); break
            raise AssertionError((label,snapshots[0][2],snapshots[1][2]))
        report['cases'].append({'name':label,'result':'pass','trace_events':len(machines[0].trace)})
    for ch in range(256):
        check(f'channel lifecycle {ch:02x}', [('sdadc_set_channel',ch),('sdadc_set_gain',0x1abf),('sdadc_set_dma_samples',32),('sdadc_start',0x80000),('sdadc_exit',0),('sdadc_start',0x81000)], seed=ch)
    for rate in (48000,44100,38000,32000,24000,22050,16000,12000,11025,8000,38400,0,1,0xffffffff):
        for ch in (0,12,0xc0,0xcc):
            for seed in range(4):
                check(f'rate {rate} channel {ch:x} seed {seed}', [('sdadc_set_channel',ch),('sdadc_set_sample_rate',rate),('sdadc_set_sample_rate',rate)],seed)
    for gain in (0,1,31,32,255,256,8191,65535):
        for count in (0,1,2,255,256,65535):
            check(f'gain {gain} count {count}', [('sdadc_set_gain',gain),('sdadc_set_dma_samples',count),('sdadc_set_channel',0xc0),('sdadc_set_gain',gain),('sdadc_start',0x81234),('sdadc_exit',0)],gain)
    for status in range(16):
        check(f'isr status {status:x}', [('sdadc_isr',0),('sdadc_isr',0)],status,status)
    for seed in range(8):
        check(f'no config {seed}', [('sdadc_start',0),('sdadc_exit',0),('sdadc_set_gain',65535),('sdadc_set_channel',0)],seed)
    (OUT/'sdadc-differential.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(report["cases"])} SDADC differential cases passed')

if __name__ == '__main__': main()
