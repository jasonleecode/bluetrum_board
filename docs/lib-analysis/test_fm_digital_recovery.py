#!/usr/bin/env python3
"""Execute original fmrx.o and recovered C under the same synthetic MMIO.

Requires clang, RISC-V ld.lld, Unicorn and pyelftools. This checks ordered
register accesses and API behavior, not hardware DMA, RF or audio timing.
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
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/fmrx_digital_recovered.c'
ANALOG, STOP = 0x60000, 0x70000
API = ('dac_sync', 'power_on', 'power_off', 'digital_stop', 'digital_start', 'get_audio_data', 'dma_to_aubuf')
REGS = (0x114, 0x140, 0x160, 0x400, 0x404, 0x408, 0x410, 0x414, 0x418, 0x41c, 0x440, 0x444)


def archive_member(name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])


def build(linker, tmp):
    original, save, recovered = (tmp / name for name in ('original.o', 'save.o', 'recovered.o'))
    original.write_bytes(archive_member('fmrx.o'))
    save.write_bytes(archive_member('save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    result = []
    for obj in (original, recovered):
        elf = obj.with_suffix('.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'fmrx_power_on', '-o', str(elf), str(obj), str(save),
                        f'--defsym=fmrx_analog_init={ANALOG}'], check=True)
        result.append(elf.read_bytes())
    return result


class Machine:
    def __init__(self, binary, registers, fifo):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name('fmrx_' + name)[0]['st_value'] for name in API}
        self.uc.mem_write(ANALOG, b'\x67\x80\x00\x00')
        self.uc.mem_write(0x50000, bytes(range(256)))
        self.regs, self.fifo, self.trace = dict(registers), list(fifo), []
        self.uc.hook_add(UC_HOOK_CODE, self.analog, begin=ANALOG, end=ANALOG)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        # Vendor restore helper uses unsupported custom instructions. Model
        # only its ABI; the function body and corresponding save run unchanged.
        entry = symbols.get_symbol_by_name('__riscv_restore_0')[0]['st_value']
        self.uc.hook_add(UC_HOOK_CODE, self.restore, begin=entry, end=entry)

    def restore(self, uc, address, size, _):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        ra = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def analog(self, uc, address, size, _):
        self.trace.append(('call', 'fmrx_analog_init'))
        # No analog/RF behavior is modeled by this isolated digital test.
        uc.reg_write(UC_RISCV_REG_A0, 0xa5a5a5a5)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        if address == 0x114 and self.fifo:
            self.regs[address] = self.fifo.pop(0)
        value = self.regs[address]
        self.trace.append(('read', address, value))
        uc.mem_write(address, struct.pack('<I', value))

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        self.regs[address] = value & 0xffffffff
        self.trace.append(('write', address, value & 0xffffffff))

    def call(self, name, *args):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(reg, value & 0xffffffff)
        self.uc.emu_start(self.api[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000, (name, 'stack imbalance')
        # All seven functions return void; residual a0 is not an API result.
        assert bytes(self.uc.mem_read(0x50000, 256)) == bytes(range(256)), 'unexpected synchronous buffer write'


def cases():
    rng = random.Random(0x464d)
    for fill in (0, 0xffffffff, 0xaaaaaaaa, 0x55555555):
        regs = dict.fromkeys(REGS, fill)
        yield f'lifecycle {fill:08x}', regs, [], [
            ('power_on', 0), ('digital_stop',), ('digital_start',),
            ('dma_to_aubuf', 1), ('dma_to_aubuf', 0), ('dma_to_aubuf', 255),
            ('get_audio_data', 0x50000, 512), ('power_off',), ('power_off',)]
    for val in (1, 0xffffffff):
        yield f'reserved power argument {val}', dict.fromkeys(REGS, 0), [], [('power_on', val)]
    for count in (0, 1, 2, 255, 256, 65536, 0xffffffff):
        for ptr in (0, 0x50000, 0x50001):
            yield f'raw DMA ptr {ptr:x} count {count}', dict.fromkeys(REGS, 0), [], [('get_audio_data', ptr, count)]
    for size in (0, 1, 2, 3, 6, 300, 1024, 0xfffffffe, 0xffffffff):
        third = size // 3
        for fifo in sorted({0, max(0, third-1), third, third+1, max(0, third*2-1), third*2, 0xffffffff}):
            yield f'sync size {size} fifo {fifo}', dict.fromkeys(REGS, 0), [fifo, fifo], [('dac_sync', size)]
    for fifo in ([101, 0], [101, 199], [101, 200], [101, 0xffffffff]):
        yield f'sync moving FIFO {fifo}', dict.fromkeys(REGS, 0), fifo, [('dac_sync', 300)]
    for i in range(32):
        regs = {reg: rng.getrandbits(32) for reg in REGS}
        actions = [('digital_start',), ('dma_to_aubuf', rng.randrange(256)),
                   ('digital_stop',), ('dac_sync', rng.getrandbits(32)), ('power_off',)]
        yield f'random register state {i}', regs, [], actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-fm-digital-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {
        'unicorn_version': unicorn.__version__,
        'original_object_sha256': hashlib.sha256(archive_member('fmrx.o')).hexdigest(),
        'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'model': 'Synthetic MMIO; fmrx_analog_init stub; __riscv_restore_0 ABI shim. No RF, DMA completion, audio format, custom-opcode or silicon timing verification.',
        'cases': [],
    }
    for name, regs, fifo, actions in cases():
        machines = [Machine(binary, regs, fifo) for binary in binaries]
        for action in actions:
            for machine in machines:
                machine.call(*action)
            assert machines[0].trace == machines[1].trace, (name, action, machines[0].trace, machines[1].trace)
            assert machines[0].regs == machines[1].regs, (name, action, 'register mismatch')
        report['cases'].append({'name': name, 'api_actions': len(actions),
                                'trace_events': len(machines[0].trace), 'result': 'pass'})
        print('PASS', name)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'fm-digital-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered RISC-V code')


if __name__ == '__main__':
    main()
