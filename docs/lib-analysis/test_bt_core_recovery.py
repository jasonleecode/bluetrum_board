#!/usr/bin/env python3
"""Differential execution of libbtctrl.a:bb.o and its C replacement.

ROM, RF and link-layer callees are hooks; MMIO status transitions are modeled.
This is not a board or RF timing test. Original custom restore instructions
use an ABI shim; the original save instructions execute unchanged.
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
from unicorn import riscv_const as R
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_core_recovered.c'
REGS = (0x64, 0x350, 0x374, 0x398, 0x39c, 0x3a8, 0x3b0, 0x3b4,
        0x3f8, 0x540, 0x544, 0x548, 0x600, 0x604, 0x60c, 0x610, 0x614,
        0x710, 0x8010, 0xf010)
MEMORY = {'.btmem.bthw': (0x11000, 1666), '.btmem.bb.buf': (0x12000, 4384),
          '.btmem.bb.sniff': (0x14000, 52), '.sbss': (0x15000, 4)}
STATE, COUNTER, NANOS, TIMER, OUTPUT = 0x14000, 0x15000, 0x104e4, 0x16000, 0x17000
STOP, STACK = 0x3f0000, 0x3e0000
SREGS = [getattr(R, f'UC_RISCV_REG_S{i}') for i in range(12)]
AREGS = [getattr(R, f'UC_RISCV_REG_A{i}') for i in range(8)]
RETURNS = ('bb_sleep', 'bb_keep_within_opening', 'bb_is_sleep', 'bb_make_on_husband')
ROM = {0x845c8: 'rom_845c8', 0x84320: 'rom_84320', 0x84324: 'rom_84324',
       0x84300: 'rom_84300', 0x84304: 'rom_84304', 0x84350: 'rom_84350',
       0x84664: 'rom_84664', 0x84308: 'rom_84308', 0x845b8: 'rom_845b8'}


def member(archive, name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib' / archive), name])


def build(args, tmp):
    original = member('libbtctrl.a', 'bb.o')
    (tmp / 'original.o').write_bytes(original)
    (tmp / 'save.o').write_bytes(member('libhal.a', 'save-restore.o'))
    original_elf = ELFFile(io.BytesIO(original))
    original_symbols = original_elf.get_section_by_name('.symtab')
    apis = {s.name: original_elf.get_section(s['st_shndx']).name
            for s in original_symbols.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'}
    externals = [s.name for s in original_symbols.iter_symbols()
                 if s['st_shndx'] == 'SHN_UNDEF' and s.name and s.name != 'nanos_env'
                 and not s.name.startswith('__riscv_')]
    hooks = {0x180000 + i * 16: name for i, name in enumerate(externals)}
    objects = ['original']
    compilers = [('clang', ['clang', '--target=riscv32-unknown-elf'])]
    if args.gcc:
        compilers.append(('gcc', [args.gcc, '-msave-restore']))
    for name, command in compilers:
        objects.append(name)
        subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding',
                        '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE),
                        '-o', str(tmp / (name + '.o'))], check=True)
        obj = ELFFile(io.BytesIO((tmp / (name + '.o')).read_bytes()))
        symbols = obj.get_section_by_name('.symtab')
        assert {s.name for s in symbols.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'
                and s['st_info']['bind'] == 'STB_GLOBAL'} == set(apis)
        for function, section in apis.items():
            symbol = symbols.get_symbol_by_name(function)[0]
            assert obj.get_section(symbol['st_shndx']).name == section, function
        for section, (_, size) in MEMORY.items():
            actual, expected = obj.get_section_by_name(section), original_elf.get_section_by_name(section)
            assert actual['sh_size'] == size and actual['sh_addralign'] == expected['sh_addralign'], section
            assert actual['sh_type'] == expected['sh_type'], (name, section)
            assert actual.data() == expected.data(), section
        for section in ('.sleep_text.bb', '.text.update'):
            assert obj.get_section_by_name(section)['sh_addralign'] == original_elf.get_section_by_name(section)['sh_addralign']
        global_data = symbols.get_symbol_by_name('bb_get_without_vehicle')[0]
        assert global_data['st_size'] == 1666 and global_data['st_info']['bind'] == 'STB_GLOBAL'
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { ' + ' '.join(f'{section} 0x{address:x} : {{ *({section}) }}'
                      for section, (address, _) in MEMORY.items()) +
                      ' .text 0x200000 : { *(.text*) *(.com_text*) } '
                      '.sleep_text 0x220000 : { *(.sleep_text*) } '
                      '.data 0x230000 : { *(.data*) *(.sdata*) *(.rodata*) } '
                      '.bss : { *(.bss*) } __global_pointer$ = 0x230800; }')
    binaries = {}
    for name in objects:
        elf = tmp / (name + '.elf')
        subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'bb_init', '-o', str(elf), str(tmp / (name + '.o')), str(tmp / 'save.o'),
                        f'--defsym=nanos_env={NANOS}',
                        *[f'--defsym={name}={address}' for address, name in hooks.items()]], check=True)
        binaries[name] = elf.read_bytes()
    listing = subprocess.check_output(['llvm-objdump-10', '--mattr=+m', '-dr', str(tmp / 'original.o')], text=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'bt-core.llvm.txt').write_text(listing.replace(str(tmp / 'original.o'), 'libbtctrl.a:bb.o'))
    return binaries, original, apis, {**hooks, **ROM}


class Machine:
    def __init__(self, binary, apis, hooks, config):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x400000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in apis}
        self.config = config
        rng = random.Random(config.get('seed', 0))
        self.regs = {address: rng.getrandbits(32) for address in REGS}
        self.regs[0x64] = (self.regs[0x64] & ~0x7c) | config.get('clock', 8)
        self.regs[0x8010], self.regs[0xf010] = config.get('irq0', 1), config.get('irq1', 1)
        for section, (address, size) in MEMORY.items():
            self.uc.mem_write(address, bytes(rng.randrange(256) for _ in range(size)))
        self.uc.mem_write(STATE, struct.pack('<HBB', config.get('flags', 0), config.get('state', 0), config.get('mode', 1)))
        self.put32(COUNTER, config.get('counter', 0))
        self.put32(NANOS + 16, TIMER if 'timer' in config else 0)
        self.put32(TIMER + 8, config.get('timer', 0))
        self.trace, self.allocations, self.control_reads = [], 0, 0
        self.waiting, self.polls, self.blocked = False, 0, False
        for address, name in hooks.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        for low, high in ((0, 0xfff), (0x8000, 0x80ff), (0xf000, 0xf0ff)):
            self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=low, end=high)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=low, end=high)
        for count in range(13):
            symbol = symbols.get_symbol_by_name(f'__riscv_restore_{count}')
            if symbol:
                address = symbol[0]['st_value']
                self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=address, end=address)
        address = self.api['bb_pass_under_iron']
        self.uc.hook_add(UC_HOOK_CODE, self.delay, begin=address, end=address)

    def put32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def get32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(R.UC_RISCV_REG_SP)
        for offset, reg in enumerate(reversed(SREGS[:count])):
            uc.reg_write(reg, self.get32(sp + offset * 4))
        ra = self.get32(sp + count * 4)
        uc.reg_write(R.UC_RISCV_REG_RA, ra)
        uc.reg_write(R.UC_RISCV_REG_SP, sp + (count + 1) * 4)
        uc.reg_write(R.UC_RISCV_REG_PC, ra)

    def delay(self, uc, address, size, _):
        self.trace.append(('six_nops',))

    def hook(self, uc, address, size, name):
        args = [uc.reg_read(reg) for reg in AREGS[:4]]
        a0, a1, a2, a3 = args
        result = 0xdeadbeef
        call_args = []
        if name == 'memset':
            call_args = args[:3]
            assert a0 == STATE and a1 == 0 and a2 == 52
        elif name == 'rom_845c8': call_args = [a0]
        elif name == 'rom_84320': call_args = args[:3]
        elif name == 'rom_84324': call_args = args[:2]
        elif name == 'rom_84300': call_args = args
        elif name in ('bb_save_about_song', 'bb_find_to_afternoon'): call_args = [a0]
        elif name == 'bb_send_without_line': call_args = args[:2]
        elif name in ('bb_pass_about_dependent', 'bb_show_with_stay'): call_args = [a0]
        elif name in ('bb_pass_within_library', 'bb_get_at_attitude', 'bb_send_to_mall'):
            call_args = [('duration', self.get32(a0))]
            if name != 'bb_pass_within_library': call_args.append(a1)
        self.trace.append(('call', name, call_args, bytes(uc.mem_read(STATE, 52)).hex(), self.get32(COUNTER)))
        if name == 'memset':
            uc.mem_write(a0, bytes([a1 & 255]) * a2)
            result = a0
        elif name == 'rom_84324':
            result = 0 if self.config.get('null_alloc') else 0x17100 + self.allocations * 0x100
            self.allocations += 1
        elif name == 'rom_84320':
            # Simulate a callee using its buffer and allocator metadata.
            uc.mem_write(a1, bytes([a0]) * min(a2, 16))
            self.put32(0x12000 + (a0 - 1) * 4, a1)
        elif name == 'rom_84308': result = self.config.get('allow', 1)
        elif name == 'rom_845b8': result = self.config.get('now', 0x12340)
        elif name == 'bb_pass_within_library':
            result = self.config.get('aux', 2)
            if 'duration0' in self.config: self.put32(a0, self.config['duration0'])
        elif name == 'bb_get_at_attitude':
            result = self.config.get('attitude', 1)
            if 'duration1' in self.config: self.put32(a0, self.config['duration1'])
        elif name == 'bb_send_to_mall':
            result = self.config.get('mall', 1)
            if 'duration2' in self.config: self.put32(a0, self.config['duration2'])
        elif name == 'bb_set_about_agent':
            self.waiting, self.polls = True, 0
            self.regs[0x3f8] &= ~0x10000
        elif name in ('bb_pass_about_dependent', 'bb_show_with_stay'):
            assert a0 == STATE + 4
            uc.mem_write(a0, bytes(range(48)))
        if self.config.get('changing'):
            self.regs[0x544] ^= 0x100
            self.regs[0x540] ^= 2
            if name == 'bb_pass_in_airport': self.regs[0xf010] ^= 1
        for i, reg in enumerate(AREGS): uc.reg_write(reg, 0xa5a50000 + i)
        uc.reg_write(R.UC_RISCV_REG_A0, result)
        uc.reg_write(R.UC_RISCV_REG_PC, uc.reg_read(R.UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        if address == 0x540 and self.config.get('changing'):
            self.regs[address] ^= 0x101
        if address == 0x3f8 and self.waiting:
            self.polls += 1
            if self.config.get('stuck'):
                if self.polls >= 12:
                    self.blocked = True
                    uc.emu_stop()
            elif self.polls > self.config.get('wait', 2):
                self.regs[address] |= 0x10000
                self.waiting = False
        self.trace.append(('read', address, self.regs[address]))
        self.put32(address, self.regs[address])

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        self.trace.append(('write', address, value))
        self.regs[address] = value

    def call(self, name, *args):
        self.uc.reg_write(R.UC_RISCV_REG_SP, STACK)
        self.uc.reg_write(R.UC_RISCV_REG_RA, STOP)
        for i, reg in enumerate(SREGS): self.uc.reg_write(reg, 0x12345600 + i)
        for i, reg in enumerate(AREGS): self.uc.reg_write(reg, args[i] if i < len(args) else 0xa5a50000 + i)
        self.uc.emu_start(self.api[name], STOP, count=100000)
        if self.blocked:
            self.trace.append(('blocked', name))
            return
        assert self.uc.reg_read(R.UC_RISCV_REG_PC) == STOP, name
        assert self.uc.reg_read(R.UC_RISCV_REG_SP) == STACK, name
        assert [self.uc.reg_read(reg) for reg in SREGS] == [0x12345600 + i for i in range(12)], name
        if name in RETURNS: self.trace.append(('return', name, self.uc.reg_read(R.UC_RISCV_REG_A0)))

    def snapshot(self):
        return (self.trace, self.regs,
                [bytes(self.uc.mem_read(address, size)) for address, size in MEMORY.values()],
                bytes(self.uc.mem_read(OUTPUT, 12)))


def cases():
    for seed in range(24):
        actions = [('bthw_get_heap_info', OUTPUT, OUTPUT + 4, OUTPUT + 8),
                   ('bb_go_about_way',), ('bb_make_on_husband',), ('bb_find_on_influence',),
                   ('bb_init',), ('bthw_soft_isr',), ('bthw_isr_do',), ('bb_run_loop',),
                   ('bb_reset',), ('bb_off',), ('bb_init',), ('bb_sleep',),
                   ('bb_sleep',), ('bb_give_on_enthusiasm',), ('bb_get_at_clue',), ('bb_sleep',)]
        yield f'lifecycle-{seed}', dict(seed=seed, changing=bool(seed & 1), null_alloc=bool(seed & 2)), actions
    for value in range(256):
        yield f'putc-{value}', dict(seed=value, changing=bool(value & 1)), [('bb_keep_within_error', value)]
    for mask in (0, 1, 8, 32, 0x8000, 0xffff, 0x10000, 0xffffffff):
        for flags in (0, 1, 32, 0xffff):
            yield f'flags-{mask:x}-{flags:x}', dict(flags=flags, seed=flags, changing=True), [
                ('bb_fetch_in_event', mask), ('bb_keep_within_opening',), ('bb_run_loop',),
                ('bb_is_sleep',), ('bb_get_at_clue',), ('bb_get_without_card', mask),
                ('bb_keep_within_opening',), ('bb_run_loop',)]
    for irq in range(4):
        for changing in (False, True):
            yield f'irq-{irq}-{changing}', dict(irq0=irq & 1, irq1=irq >> 1, changing=changing), [('bthw_isr_do',)]
    sleep_configs = [dict(state=2), dict(allow=0), dict(flags=1), dict(flags=0x8000),
                     dict(attitude=0, aux=0xff), dict(mall=0, aux=0xff), dict(stuck=True)]
    for clock in (0, 4, 8, 12, 0x28, 0x18, 0x78):
        for mode in (0, 1, 255):
            for changing in (False, True):
                sleep_configs.append(dict(clock=clock, mode=mode, changing=changing))
    for now in (0, 1, 11, 12, 15, 0xfffff, 0x7ffffff, 0xffffffff):
        for delta in (-1, 0, 1, 2, 50, 1000, 0xffff):
            sleep_configs.append(dict(now=now, timer=((now >> 4) + delta) & 0xffffffff))
    for duration in (0, 1, 2, 3, 4, 5, 701, 702, 703, 16000, 0xffffffff):
        for counter in (0, 1, 699, 700, 701, 0xffffffff):
            sleep_configs.append(dict(duration2=duration, counter=counter, aux=0x102, mall=0x101))
    for stage in range(3):
        sleep_configs.append({f'duration{stage}': 500, 'wait': 0, 'aux': 0})
    for index, config in enumerate(sleep_configs):
        yield f'sleep-{index}', dict(seed=index, **config), [('bb_sleep',)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    parser.add_argument('--gcc', help='also compare this RV32 GCC compiler against the original')
    args = parser.parse_args()
    if not args.linker: parser.error('supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-core-') as directory:
        binaries, original, apis, hooks = build(args, Path(directory))
    report = {'original_object_sha256': hashlib.sha256(original).hexdigest(),
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'unicorn_version': unicorn.__version__, 'compilers': list(binaries)[1:],
              'exports': sorted(apis),
              'model': 'Original RV32 vs recovered C; ROM/RF/link-layer hooks, modeled MMIO and save/restore ABI shim. Data layout and ordered MMIO/callee traces checked; no ROM implementation, hardware timing or radio validation.',
              'cases': []}
    for name, config, actions in cases():
        machines = {key: Machine(binary, apis, hooks, config) for key, binary in binaries.items()}
        for action in actions:
            for machine in machines.values(): machine.call(*action)
            expected = machines['original'].snapshot()
            for key, machine in machines.items():
                actual = machine.snapshot()
                if actual != expected:
                    for index, (left, right) in enumerate(zip(expected[0], actual[0])):
                        if left != right:
                            raise AssertionError((name, key, action, index, left, right))
                    raise AssertionError((name, key, action, 'trace lengths', len(expected[0]), len(actual[0]),
                                          'state equal', expected[1:] == actual[1:]))
        report['cases'].append({'name': name, 'actions': len(actions), 'result': 'pass'})
    (OUT / 'bt-core-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; {len(apis)} exports, original vs {report["compilers"]}')


if __name__ == '__main__':
    main()
