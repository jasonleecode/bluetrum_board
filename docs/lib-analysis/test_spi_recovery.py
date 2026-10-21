#!/usr/bin/env python3
"""Differential execution of original and recovered SPI RISC-V machine code.

Requires clang, RISC-V-capable ld.lld (or --linker), unicorn and pyelftools.
The MMIO model is a test fixture, not proof of silicon behavior or bus timing.
Both binaries execute under the SAME model; ordered reads/writes, hooks and
API return values must agree. No actual board or peripheral is accessed.
"""
import argparse
import hashlib
import io
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_RA,
    UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
HOOKS = {name: 0x60000 + 16*i for i, name in enumerate((
    'register_isr', 'get_sysclk_nhz', 'hal_get_ticks',
    'os_interrupt_enter', 'os_interrupt_leave', 'callback'))}
STOP = 0x70000
CON, BUF, CPND = 0x980, 0x984, 0x98c
API = ('version', 'init', 'deinit', 'power', 'send', 'recv', 'send_byte', 'recv_byte', 'control')


def build(linker, tmp):
    original = tmp / 'original.o'
    original.write_bytes(subprocess.check_output(['ar', 'p', str(ROOT/'bare/lib/libhal.a'), 'driver_spi.o']))
    save = tmp / 'save.o'
    save.write_bytes(subprocess.check_output(['ar', 'p', str(ROOT/'bare/lib/libhal.a'), 'save-restore.o']))
    recovered = tmp / 'recovered.o'
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'bare/drivers/inc'), '-c',
                    str(ROOT/'bare/drivers/src/driver_spi_recovered.c'), '-o', str(recovered)], check=True)
    script = tmp/'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text.spi) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for obj in (original, recovered):
        elf = tmp/(obj.stem+'.elf')
        command = [linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                   '-e', 'blue_spi1', '-o', str(elf), str(obj), str(save)]
        command += [f'--defsym={name}={address}' for name,address in HOOKS.items() if name!='callback']
        subprocess.run(command, check=True)
        binaries.append(elf.read_bytes())
    wrapper = tmp/'wrapper.o'
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'bare/drivers/inc'), '-c', str(ROOT/'bare/drivers/src/drv_spi.c'),
                    '-o', str(wrapper)], check=True)
    linkmap = tmp/'integration.map'
    subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                    '-e', 'drv_spi_init', '-o', str(tmp/'integration.elf'),
                    '-Map='+str(linkmap), str(wrapper), str(recovered), str(ROOT/'bare/lib/libhal.a'),
                    *[f'--defsym={name}={address}' for name,address in HOOKS.items() if name!='callback']], check=True)
    assert 'libhal.a(' not in linkmap.read_text(), 'Vendor archive unexpectedly pulled into SPI link'
    assert 'blue_spi1' in linkmap.read_text()
    return binaries


class Machine:
    def __init__(self, binary, tick=0, completion=2):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('blue_spi1')[0]
        self.api = dict(zip(API, struct.unpack('<9I', self.uc.mem_read(symbol['st_value'], 36))))
        for addr in HOOKS.values():
            self.uc.mem_write(addr, b'\x67\x80\x00\x00') # ret
        self.trace = []
        self.regs = {}
        self.tick = tick
        self.completion = completion
        self.pending_reads = None
        self.irq = None
        self.uc.hook_add(UC_HOOK_CODE, self.hook, begin=min(HOOKS.values()), end=max(HOOKS.values()))
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)
        # Vendor restore helpers contain custom opcode 0x0b, unsupported by
        # Unicorn. Model their ABI from the matching save helper stack layout;
        # execute the SPI body and save helpers unchanged. This does NOT verify
        # those custom instructions or their silicon implementation.
        for count in (0, 3):
            entry = elf.get_section_by_name('.symtab').get_symbol_by_name(f'__riscv_restore_{count}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, user_data=count, begin=entry, end=entry)

    def restore(self, uc, address, size, count):
        registers = ([UC_RISCV_REG_S2, UC_RISCV_REG_S1, UC_RISCV_REG_S0] if count else []) + [UC_RISCV_REG_RA]
        sp = uc.reg_read(UC_RISCV_REG_SP)
        for register in registers:
            value = struct.unpack('<I', uc.mem_read(sp, 4))[0]
            uc.reg_write(register, value)
            sp += 4
        uc.reg_write(UC_RISCV_REG_SP, sp)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def hook(self, uc, address, size, _):
        names = {v:k for k,v in HOOKS.items()}
        if address not in names:
            raise AssertionError(f'Unexpected stub address {address:#x}')
        name = names[address]
        value = 0
        if name == 'register_isr':
            self.irq = uc.reg_read(UC_RISCV_REG_A1)
            self.trace.append((name, uc.reg_read(UC_RISCV_REG_A0)))
        elif name == 'get_sysclk_nhz':
            value = 48000000
            self.trace.append((name, value))
        elif name == 'hal_get_ticks':
            value = self.tick
            self.tick = (self.tick + 1) & 0xffffffff
            self.trace.append((name, value))
        elif name == 'callback':
            self.trace.append((name, uc.reg_read(UC_RISCV_REG_A0)))
        else:
            self.trace.append((name,))
        uc.reg_write(UC_RISCV_REG_A0, value)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4
        if address == CON and self.pending_reads is not None:
            self.pending_reads -= 1
            if self.pending_reads <= 0:
                self.regs[CON] = self.regs.get(CON, 0) | 0x10000
                self.pending_reads = None
        value = self.regs.get(address, 0)
        self.trace.append(('read', address, value))
        uc.mem_write(address, struct.pack('<I', value))

    def write(self, uc, access, address, size, value, _):
        assert size == 4
        value &= 0xffffffff
        self.trace.append(('write', address, value))
        if address == 0x20:
            # Model per-nibble command behavior: 0=no-op, 15=clear, else select.
            prior = self.regs.get(address, 0)
            for shift in range(0, 32, 4):
                nibble = (value >> shift) & 15
                if nibble:
                    prior = (prior & ~(15 << shift)) | ((0 if nibble==15 else nibble) << shift)
            self.regs[address] = prior
        else:
            self.regs[address] = value
        if address == CPND:
            self.regs[CON] = self.regs.get(CON, 0) & ~value
        if address == BUF:
            self.regs[CON] = self.regs.get(CON, 0) & ~0x10000
            self.regs[BUF] = 0xa5 # Fixture RX value, not a silicon claim.
            self.pending_reads = self.completion

    def call(self, name, *args):
        if name == 'status':
            self.regs[CON] = args[0]
            return None
        address = self.irq if name == 'irq' else self.api[name]
        assert address is not None
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(reg, value & 0xffffffff)
        self.uc.emu_start(address, STOP, count=100000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, f'{name} did not return'
        result = self.uc.reg_read(UC_RISCV_REG_A0)
        if name == 'version':
            return bytes(self.uc.mem_read(result, 4)).hex()
        return None if name == 'irq' else result


def cases():
    yield 'disabled DMA', [('init', 0), ('send', 0x50000, 8), ('recv', 0x51000, 8)], {}
    for mode in (1, 2, 3):
        for mapping in range(1, 6):
            yield f'role {mode} mapping {mapping}', [
                ('version',), ('init', HOOKS['callback']), ('control', 0x21, 0),
                ('control', mode, 1000000), ('control', 0x20, mapping), ('control', 0x21, 0),
                ('send', 0x50000, 16), ('status', 0x10081), ('irq',),
                ('recv', 0x51000, 32), ('status', 0x10091), ('irq',),
                ('status', 1), ('irq',), ('send_byte', 0x5a, 20), ('recv_byte', 20),
                ('power', 0), ('deinit',)], {}
    for mapping in (0, 6, 0xffffffff):
        yield f'invalid mapping {mapping}', [('init', 0), ('control', 1, 1000000),
            ('control', 0x20, mapping), ('control', 0x21, 0)], {}
    for mode in (0, 4, 31, 34, 0x101, 0x120):
        yield f'control edge {mode}', [('init', 0), ('control', mode, 1000000)], {}
    yield 'mode transitions', [('init', 0), ('control', 3, 1000000), ('control', 0x20, 4),
        ('control', 0x21, 0), ('control', 1, 2000000), ('control', 0x21, 0),
        ('control', 0x20, 1), ('control', 3, 1000000), ('control', 2, 0)], {}
    for speed in (0, 1, 48000000, 96000000):
        yield f'divider {speed}', [('init', 0), ('control', 1, speed), ('control', 3, speed)], {}
    yield 'timeout', [('init', 0), ('send_byte', 0xff, 3), ('recv_byte', 3)], {'completion': None}
    yield 'tick wrap quirk', [('init', 0), ('send_byte', 0xff, 10)], {'tick': 0xfffffffd, 'completion': None}
    yield 'zero/negative timeout completes', [('init', 0), ('send_byte', 0x55, 0),
                                             ('recv_byte', -1)], {}
    yield 'DMA zero count and no callback', [('init', 0), ('control', 1, 1000000),
        ('control', 0x20, 4), ('control', 0x21, 0), ('send', 0, 0),
        ('recv', 0, 0), ('status', 0x10091), ('irq',)], {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ab32-spi-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {'unicorn_version': unicorn.__version__,
              'original_object_sha256': hashlib.sha256(subprocess.check_output([
                  'ar','p',str(ROOT/'bare/lib/libhal.a'),'driver_spi.o'])).hexdigest(),
              'recovered_source_sha256': hashlib.sha256((ROOT/'bare/drivers/src/driver_spi_recovered.c').read_bytes()).hexdigest(),
              'integration_smoke': 'Wrapper + recovered driver linked with platform stubs; no libhal.a member extracted.',
              'model': 'Synthetic MMIO, platform hooks and __riscv_restore_0/3 ABI; no silicon/bus timing or custom-opcode verification.', 'cases': []}
    for name, actions, options in cases():
        machines = [Machine(b, **options) for b in binaries]
        for action in actions:
            results = [m.call(*action) for m in machines]
            assert results[0] == results[1], (name, action, results)
            if machines[0].trace != machines[1].trace:
                for i, (a,b) in enumerate(zip(machines[0].trace, machines[1].trace)):
                    if a != b:
                        raise AssertionError((name, action, i, a, b))
                raise AssertionError((name, action, 'trace lengths differ'))
            assert machines[0].regs == machines[1].regs, (name, action, 'register states differ')
        report['cases'].append({'name': name, 'api_actions': len(actions),
                                'trace_events': len(machines[0].trace), 'result': 'pass'})
        print('PASS', name)
    (OUT/'spi-differential.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered RISC-V code')


if __name__ == '__main__':
    main()
