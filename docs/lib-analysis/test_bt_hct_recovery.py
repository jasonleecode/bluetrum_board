#!/usr/bin/env python3
"""Differentially execute hct_msg.o/hct_proc.o and recovered RV32 C.

ROM allocation/queue services and the rest of the controller are stubs. This
validates message bytes, command credits, event masks, tables and call ABI;
it does not emulate the radio, scheduler or complete Bluetooth protocol.
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
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
    UC_RISCV_REG_A2, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC,
    UC_RISCV_REG_S0, UC_RISCV_REG_S1)
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_hct_recovered.c'
STATE, MESSAGE, DESCRIPTOR, STOP = 0x30000, 0x4d100, 0x4e000, 0x70000
TABLES = dict(zip(('hct_fetch_on_extreme', 'hct_give_within_test',
                  'hct_go_of_garage', 'hct_go_inside_house', 'hct_send_in_present'),
                 (0x40000, 0x42000, 0x44000, 0x46000, 0x48000)))
EXTERNALS = {'hct_leave_with_ability': 0x60000, 'hct_take_under_cook': 0x60010,
             'bb_get_without_card': 0x60020, 'my_printf': 0x60030}
FORMATTER = 0x60040
ROM = {0x843a4: 'send', 0x84328: 'free', 0x843c8: 'queue'}
ARGREGS = (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2)


def member(archive, name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib' / archive), name])


def build(linker, tmp):
    objects = []
    for name in ('hct_msg.o', 'hct_proc.o'):
        path = tmp / name
        path.write_bytes(member('libbtctrl.a', name))
        objects.append(path)
    save = tmp / 'save.o'
    save.write_bytes(member('libhal.a', 'save-restore.o'))
    recovered = tmp / 'recovered.o'
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.vector*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x31000; .data : { *(.data*) *(.sdata*) *(.btmem*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x31800; }')
    definitions = {**TABLES, **EXTERNALS, 'lm_proc_pass_to_knee': STATE}
    binaries = []
    for name, inputs in [('original', objects), ('recovered', [recovered])]:
        elf = tmp / (name + '.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'hct_get_on_birth', '-o', str(elf), *map(str, inputs), str(save),
                        *[f'--defsym={key}={value}' for key, value in definitions.items()]], check=True)
        binaries.append(elf.read_bytes())
    disassembler = shutil.which('llvm-objdump-10') or shutil.which('llvm-objdump')
    if disassembler:
        OUT.mkdir(parents=True, exist_ok=True)
        listing = subprocess.check_output([disassembler, '-dr', *map(str, objects)])
        for obj in objects:
            listing = listing.replace(str(obj).encode(), ('libbtctrl.a:' + obj.name).encode())
        (OUT / 'bt-hct.llvm.txt').write_bytes(listing)
    return binaries


class Machine:
    def __init__(self, binary, seed=0, leave_value=0, event=0x0e, offset=-5, formatted_length=6):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        rng = random.Random(seed)
        self.uc.mem_write(STATE, bytes(rng.randrange(256) for _ in range(256)))
        self.uc.mem_write(0x4d000, bytes(rng.randrange(256) for _ in range(0x2000)))
        self.trace = []
        self.leave_value, self.event, self.offset = leave_value, event, offset
        self.formatted_length = formatted_length
        for address in list(EXTERNALS.values()) + [FORMATTER] + list(ROM):
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.external, begin=address, end=address)
        for n in (0, 2):
            address = self.symbols['__riscv_restore_' + str(n)]
            self.uc.hook_add(UC_HOOK_CODE, self.restore, user_data=n, begin=address, end=address)

    def restore(self, uc, address, size, n):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        values = struct.unpack('<' + 'I' * (n + 1), uc.mem_read(sp, 4 * (n + 1)))
        for reg, value in zip((UC_RISCV_REG_S1, UC_RISCV_REG_S0) if n == 2 else (), values):
            uc.reg_write(reg, value)
        uc.reg_write(UC_RISCV_REG_SP, sp + 4 * (n + 1))
        uc.reg_write(UC_RISCV_REG_RA, values[-1])
        uc.reg_write(UC_RISCV_REG_PC, values[-1])

    def external(self, uc, address, size, _):
        a, b, c = [uc.reg_read(r) for r in ARGREGS]
        if address == EXTERNALS['hct_leave_with_ability']:
            self.trace.append(('leave_with_ability', a, b))
            uc.mem_write(a + 10, struct.pack('<H', self.leave_value))
        elif address == EXTERNALS['hct_take_under_cook']:
            self.trace.append(('take_under_cook',))
        elif address == EXTERNALS['bb_get_without_card']:
            self.trace.append(('get_without_card', a))
        elif address == EXTERNALS['my_printf']:
            fmt = bytes(uc.mem_read(a, 64)).split(b'\0')[0].decode()
            self.trace.append(('printf', fmt, b))
        elif address == FORMATTER:
            self.trace.append(('formatter', a, b, c))
            uc.mem_write(a + 16 + self.offset, bytes([self.event]))
        else:
            self.trace.append((ROM[address], a, b) if ROM[address] == 'queue' else (ROM[address], a))
        uc.reg_write(UC_RISCV_REG_A0, self.formatted_length if address == FORMATTER else 0xdeadbeef)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def put16(self, address, value):
        self.uc.mem_write(address, struct.pack('<H', value & 0xffff))

    def descriptor(self, address, offset, length, value):
        self.uc.mem_write(address, struct.pack('<bBHI', offset, length, value, FORMATTER))

    def call(self, name, *args):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip(ARGREGS, args):
            self.uc.reg_write(reg, value & 0xffffffff)
        self.uc.emu_start(self.symbols[name], STOP, count=10000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000, (name, 'stack imbalance')
        return self.uc.reg_read(UC_RISCV_REG_A0)

    def snapshot(self):
        return (bytes(self.uc.mem_read(STATE, 256)), bytes(self.uc.mem_read(0x4d000, 0x2000)), self.trace)

    def table(self):
        base = self.symbols['hct_cause_without_cup']
        pointer, count, reserved = struct.unpack('<IHH', self.uc.mem_read(base, 8))
        assert pointer == self.symbols['hct_proc_sell_to_horror']
        rows = [struct.unpack('<HHI', self.uc.mem_read(pointer + 8*i, 8)) for i in range(count)]
        assert rows[0][2] == self.symbols['hct_proc_tell_for_efficiency']
        assert rows[1][2] == self.symbols['hct_proc_cause_under_statement']
        return count, reserved, [(row[0], row[1]) for row in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-hct-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {'unicorn_version': unicorn.__version__,
              'original_object_sha256': {name: hashlib.sha256(member('libbtctrl.a', name)).hexdigest()
                                         for name in ('hct_msg.o', 'hct_proc.o')},
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'model': 'ROM queue/send/free and controller calls stubbed; restore_0/2 ABI shim; compare mutable memory, external call order and non-void results. No RF, scheduling, allocator, protocol or physical hardware validation.',
              'cases': []}

    def check(label, function, arguments, setup=lambda m: None, returns=True, **options):
        machines = [Machine(binary, **options) for binary in binaries]
        results = []
        for machine in machines:
            setup(machine)
            results.append(machine.call(function, *arguments))
        assert machines[0].snapshot() == machines[1].snapshot(), (label, 'memory/call mismatch', machines[0].trace, machines[1].trace)
        if returns:
            assert results[0] == results[1], (label, results)
        report['cases'].append({'name': label, 'result': 'pass'})

    for seed in range(12):
        check(f'send {seed}', 'hct_get_on_birth', (MESSAGE + 16,), returns=False, seed=seed)
        for value in (0, 1, 0x100, 0xffff):
            check(f'ownership {seed} {value}', 'hct_find_with_problem', (MESSAGE + 16,),
                  returns=False, seed=seed, leave_value=value)
        check(f'route {seed}', 'hct_msg_do_in_vacation', (MESSAGE + 16,), returns=False, seed=seed)
        for value in (0, 1, 255):
            check(f'primary {seed} {value}', 'hct_msg_cause_of_primary', (MESSAGE, value), returns=False, seed=seed)

    for offset in (-128, -5, -4, 0, 4, 127):
        for length in (0, 1, 250, 255):
            for credit in (0, 254, 255):
                for opcode in (0x10a0, 0x10a1):
                    def setup(m, offset=offset, length=length, credit=credit, opcode=opcode):
                        m.descriptor(DESCRIPTOR, offset, length, 0xabcd)
                        m.put16(MESSAGE + 8, opcode)
                        m.uc.mem_write(STATE + 3, bytes([credit]))
                        m.uc.mem_write(MESSAGE + 14, b'\xfe')
                    check(f'command complete {offset} {length} {credit} {opcode:x}',
                          'hct_cause_to_ladder', (MESSAGE, DESCRIPTOR), setup)

    for function in ('hct_give_without_resolve', 'hct_save_within_tea'):
        for value in (0, 1, 0xff, 0x100, 0xffff):
            for length in (0, 1, 255):
                for seed in range(4):
                    check(f'{function} {value} {length} {seed}', function, (MESSAGE, DESCRIPTOR),
                          lambda m, length=length, value=value: m.descriptor(DESCRIPTOR, -5, length, value), seed=seed)

    machines = [Machine(binary) for binary in binaries]
    assert machines[0].table() == machines[1].table() == (2, 0, [(0xffff, 0), (0x2401, 0)])
    for machine in machines:
        assert machine.uc.mem_read(machine.symbols['hct_find_without_crack'], 1) == b'\0'
    report['cases'].append({'name': 'dispatch table layout and initial state', 'result': 'pass'})
    # Exhaustive 16-bit lookup catches the unusual group arithmetic and lack
    # of per-table bounds checking, without dereferencing invalid results.
    for id_value in range(65536):
        results = [m.call('hct_proc_send_on_network', id_value) for m in machines]
        assert results[0] == results[1], (id_value, results)
    report['cases'].append({'name': 'all 65536 internal descriptor IDs', 'inputs': 65536, 'result': 'pass'})
    for id_value in (0x4000000, 0x4000400, 0xffffffff):
        check(f'wide lookup {id_value:x}', 'hct_proc_send_on_network', (id_value,))

    check('controller event acknowledgement', 'hct_proc_cause_under_statement', (0x2401, MESSAGE + 16))
    for id_value in (0x147d, 0x822):
        check(f'ACL queue {id_value:x}', 'hct_proc_tell_for_efficiency', (id_value, MESSAGE + 16))
    check('unknown internal descriptor', 'hct_proc_tell_for_efficiency', (0x1234, MESSAGE + 16),
          lambda m: m.put16(MESSAGE + 8, 0x0c01))
    for id_value in (0x422, 0x80c, 0x1066, 0x1427, 0x2012):
        for event in (0, 1, 8, 9, 14, 15, 64, 240, 241, 255):
            for enabled in (False, True):
                for offset in (-5, 0):
                    def setup(m, id_value=id_value, enabled=enabled, offset=offset):
                        m.put16(MESSAGE + 8, id_value)
                        m.uc.mem_write(STATE + 84, bytes([255 if enabled else 0]) * 32)
                        address = m.call('hct_proc_send_on_network', id_value)
                        m.descriptor(address, offset, 6, 0xabcd)
                    check(f'event filter {id_value:x} {event} {enabled} {offset}',
                          'hct_proc_tell_for_efficiency', (0x8888, MESSAGE + 16), setup,
                          event=event, offset=offset, formatted_length=0x10005)

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'bt-hct-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed, including exhaustive 65536-ID lookup; original vs recovered RV32')


if __name__ == '__main__':
    main()
