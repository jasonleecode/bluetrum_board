#!/usr/bin/env python3
"""Compare vendor usb_device_hid.o and recovered C using RV32 execution.

Endpoint operations/memset are deterministic hooks, not USB wire emulation.
The original save helpers execute; restore custom opcodes use an ABI shim.
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
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
    UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC, UC_RISCV_REG_S0,
    UC_RISCV_REG_S1, UC_RISCV_REG_S2)
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/usb_class_recovered.c'
API = ('ude_tmr_isr', 'ude_hid_setvalid', 'usbd_ep3_transmit', 'usb_int_ep_reset', 'udh_init')
HOOKS = ('memset', 'usb_set_cur_ep', 'usb_ep_start_transfer', 'usb_ep_transfer', 'usb_ep_reset', 'usb_ep_init')
HOOK_ADDR = {name: 0x60000 + i * 16 for i, name in enumerate(HOOKS)}
GLOBALS = {'udh_0': 0x40000, 'ep3_buf': 0x41000, 'epsize_tbl': 0x42000}
EP, PREVIOUS, OTHER, BUFFER, STOP = 0x43000, 0x44000, 0x45000, 0x50000, 0x70000


def member(name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])


def write_static_audit(directory):
    """Keep exact bytes/symbols behind the descriptor and class audit."""
    report, disassembly = {'objects': {}, 'descriptors': {}}, []
    for name in ('usb_device_run_loop.o', 'usb_device_hid.o', 'usb_device_audio.o', 'usb_device_table.o'):
        data = member(name)
        path = directory / name
        path.write_bytes(data)
        elf = ELFFile(io.BytesIO(data))
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        report['objects'][name] = {
            'sha256': hashlib.sha256(data).hexdigest(),
            'exports': [{'name': sym.name, 'size': sym['st_size'], 'binding': sym['st_info']['bind'],
                         'kind': sym['st_info']['type']} for sym in symbols if sym.name and
                        sym['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK') and sym['st_shndx'] != 'SHN_UNDEF'],
            'undefined': [sym.name for sym in symbols if sym.name and sym['st_shndx'] == 'SHN_UNDEF']}
        output = subprocess.check_output(['llvm-objdump-10', '-s', '-dr', str(path)], text=True)
        disassembly.append(output.replace(str(path), 'libhal.a:' + name))
        if name == 'usb_device_table.o':
            for sym in symbols:
                if sym['st_info']['type'] != 'STT_OBJECT':
                    continue
                section = elf.get_section(sym['st_shndx'])
                raw = section.data()[sym['st_value']:sym['st_value'] + sym['st_size']]
                report['descriptors'][sym.name] = {'length': len(raw), 'hex': raw.hex()}
    comm = ELFFile(io.BytesIO(member('usb_comm.o')))
    sym = comm.get_section_by_name('.symtab').get_symbol_by_name('epsize_tbl')[0]
    section = comm.get_section(sym['st_shndx'])
    report['epsize_tbl'] = list(section.data()[sym['st_value']:sym['st_value'] + sym['st_size']])
    report['interpretation'] = {
        'device': {'bcdUSB': '0x0110', 'vid': '0x8087', 'pid': '0x1024', 'ep0_max_packet': 64},
        'hid': {'endpoint': '0x83', 'direction': 'IN', 'transfer': 'interrupt',
                'descriptor_max_packet': 16, 'interval': 1, 'report_descriptor_bytes': 33,
                'report_bytes': 1, 'report_id_present': False,
                'usage_page': 'Consumer (0x0c)', 'usages_lsb_first': ['0xb3', '0xb5', '0xb6', '0xb7', '0xcd', '0xe2', '0xe9', '0xea'],
                'allocation_bytes': 8, 'initialized_ep_max_packet': 1024},
        'speaker': {'endpoint': '0x02', 'direction': 'OUT', 'transfer': 'isochronous',
                    'descriptor_max_packet': 192, 'sample_rate': 48000, 'channels': 2,
                    'sample_bits': 16, 'initialized_ep_max_packet': 256},
        'microphone': {'endpoint': '0x82', 'direction': 'IN', 'transfer': 'isochronous',
                       'descriptor_max_packet': 192, 'sample_rate': 48000, 'channels_variants': [1, 2], 'sample_bits': 16},
        'storage': {'endpoints': ['0x01', '0x81'], 'transfer': 'bulk', 'max_packet': 64},
        'cdc_descriptor_present': False,
        'configuration_is_template': True,
        'ude_hid_send_implementation': 'Undefined in supplied archive; no ROM address established.'}
    (OUT / 'usb-class-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'usb-class.llvm.txt').write_text('\n'.join(disassembly))


def build(linker, directory):
    original, recovered, save = (directory / n for n in ('original.o', 'recovered.o', 'save.o'))
    original.write_bytes(member('usb_device_hid.o'))
    save.write_bytes(member('save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
        '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = directory / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.com_text*) *(.usbdev.com) *(.vector) } '
        '. = 0x20000; .rodata : { *(.rodata*) } . = 0x30000; .data : { *(.data*) *(.sdata*) } '
        '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    binaries = []
    for obj in (original, recovered):
        elf = obj.with_suffix('.elf')
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
            '-e', 'udh_init', '-o', str(elf), str(obj), str(save),
            *[f'--defsym={name}={address}' for name, address in {**GLOBALS, **HOOK_ADDR}.items()]], check=True)
        binaries.append(elf.read_bytes())
    OUT.mkdir(parents=True, exist_ok=True)
    write_static_audit(directory)
    text = subprocess.check_output(['llvm-objdump-10', '-dr', str(original)], text=True)
    (OUT / 'usb-hid.llvm.txt').write_text(text.replace(str(original), 'libhal.a:usb_device_hid.o'))
    return binaries


class Machine:
    def __init__(self, binary, case):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {name: symbols.get_symbol_by_name(name)[0]['st_value'] for name in API}
        for name, address in HOOK_ADDR.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        for count in (1, 2, 3):
            address = symbols.get_symbol_by_name(f'__riscv_restore_{count}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=address, end=address)
        self.trace = []
        self.case = case
        self.polls = 0
        self.selected = PREVIOUS
        self.timer_reads = list(case.get('timer_reads', []))
        self.uc.mem_write(GLOBALS['udh_0'], struct.pack('<I', EP) + case['state'])
        self.uc.mem_write(OTHER, struct.pack('<I', EP) + case['state'])
        self.uc.mem_write(EP, case['endpoint'])
        self.uc.mem_write(PREVIOUS, bytes(range(24)))
        self.uc.mem_write(GLOBALS['ep3_buf'], b'abcdefgh')
        self.uc.mem_write(GLOBALS['epsize_tbl'], bytes((1, 1, 3, case['ep_size'])))
        self.uc.mem_write(BUFFER, bytes(range(64)))
        self.uc.hook_add(UC_HOOK_MEM_READ, self.timer_read, begin=GLOBALS['udh_0'] + 5, end=GLOBALS['udh_0'] + 5)

    def timer_read(self, uc, access, address, size, value, _):
        if self.timer_reads:
            uc.mem_write(address, bytes((self.timer_reads.pop(0),)))

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)[:count]):
            value = struct.unpack('<I', uc.mem_read(sp + (count - 1 - i) * 4, 4))[0]
            uc.reg_write(reg, value)
        ra = struct.unpack('<I', uc.mem_read(sp + count * 4, 4))[0]
        uc.reg_write(UC_RISCV_REG_RA, ra)
        uc.reg_write(UC_RISCV_REG_SP, sp + (count + 1) * 4)
        uc.reg_write(UC_RISCV_REG_PC, ra)

    def state(self):
        return [bytes(self.uc.mem_read(address, count)).hex() for address, count in
                ((GLOBALS['udh_0'], 8), (EP, 24), (OTHER, 8), (GLOBALS['ep3_buf'], 8), (BUFFER, 64))]

    def hook(self, uc, address, size, name):
        a0, a1, a2 = (uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        arguments = [a0, a1, a2] if name == 'memset' else [a0, a1] if name == 'usb_ep_start_transfer' else [a0]
        self.trace.append((name, arguments, self.state()))
        result = 0xa5a5a5a5
        if name == 'memset':
            uc.mem_write(a0, bytes((a1 & 255,)) * a2)
            result = a0
        elif name == 'usb_set_cur_ep':
            result, self.selected = self.selected, a0
        elif name == 'usb_ep_transfer':
            self.polls += 1
            result = int(self.polls >= self.case['complete_after'])
            if self.case.get('tick'):
                value = uc.mem_read(GLOBALS['udh_0'] + 5, 1)[0]
                uc.mem_write(GLOBALS['udh_0'] + 5, bytes((max(0, value - 1),)))
        elif name == 'usb_ep_reset':
            uc.mem_write(EP + 6, bytes((uc.mem_read(EP + 6, 1)[0] & ~4,)))
        uc.reg_write(UC_RISCV_REG_A0, result)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def call(self, action):
        name, *args = action
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(reg, value)
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)):
            self.uc.reg_write(reg, 0x12340000 + i)
        self.uc.emu_start(self.api[name], STOP, count=10000)
        returned = self.uc.reg_read(UC_RISCV_REG_PC) == STOP
        if self.case.get('nonreturn'):
            assert not returned, 'busy transfer without timer unexpectedly returned'
            return False
        assert returned, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2)):
            assert self.uc.reg_read(reg) == 0x12340000 + i, (name, 'callee-saved register')
        return True


def cases():
    rng = random.Random(0x555342)
    def case(name, actions, **kw):
        return dict(name=name, actions=actions, state=bytes(rng.randrange(256) for _ in range(4)),
                    endpoint=bytes(rng.randrange(256) for _ in range(24)), ep_size=kw.pop('ep_size', 16),
                    complete_after=kw.pop('complete_after', 1), **kw)
    for value in (0, 1, 2, 9, 10, 29, 30, 127, 254, 255):
        c = case(f'timer {value}', [('ude_tmr_isr',)] * 3)
        c['state'] = bytes((0x55, value, 0xa5, 0x5a))
        yield c
    for reads in ([1, 0], [1, 255], [255, 1], [0]):
        yield case(f'volatile timer reads {reads}', [('ude_tmr_isr',)], timer_reads=reads)
    for size in (0, 1, 2, 3, 4, 16, 255):
        for pointer in (GLOBALS['udh_0'], OTHER):
            yield case(f'init size {size} context {pointer:x}', [('udh_init', pointer)], ep_size=size)
    for i in range(32):
        yield case(f'lifecycle random {i}', [('udh_init', GLOBALS['udh_0']), ('ude_hid_setvalid',),
            ('ude_tmr_isr',), ('usb_int_ep_reset',), ('ude_tmr_isr',)])
    for length in (0, 1, 8, 16, 64, 255, 65535):
        for after, tick in ((1, False), (2, False), (5, True), (30, True), (31, True), (99999, True)):
            yield case(f'transmit length {length} complete {after} tick {tick}',
                [('usbd_ep3_transmit', BUFFER, length)], complete_after=after, tick=tick)
    yield case('busy without timer does not return', [('usbd_ep3_transmit', BUFFER, 1)],
               complete_after=99999, nonreturn=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-usb-hid-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {'original_object_sha256': hashlib.sha256(member('usb_device_hid.o')).hexdigest(),
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'unicorn_version': unicorn.__version__,
              'model': 'RV32 bodies and original save helpers; restore_1/2/3 ABI shims; endpoint and memset call hooks. Compare ordered calls, state at calls, final RAM and register preservation. No hardware USB, IRQ cadence, DMA completion or silicon timing validation.',
              'cases': []}
    for case in cases():
        machines = [Machine(binary, case) for binary in binaries]
        for action in case['actions']:
            returned = [machine.call(action) for machine in machines]
            assert returned[0] == returned[1]
            if case.get('nonreturn'):
                # Instruction count differs by compilation. Both remain in
                # the same pending loop; compare common call prefix only.
                n = min(len(m.trace) for m in machines)
                assert machines[0].trace[:n] == machines[1].trace[:n]
            else:
                assert machines[0].trace == machines[1].trace, (case['name'], action, 'call trace')
            assert machines[0].state() == machines[1].state(), (case['name'], action, 'RAM')
            assert machines[0].selected == machines[1].selected
        report['cases'].append({'name': case['name'], 'result': 'pass', 'actions': len(case['actions'])})
        print('PASS', case['name'])
    (OUT / 'usb-class-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; original vs recovered RV32 HID')


if __name__ == '__main__':
    main()
