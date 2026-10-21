#!/usr/bin/env python3
"""Differential RV32 execution of vendor usb_device_enum.o and recovered EP0 C.

Synthetic MMIO, callbacks and memcpy; no host, descriptors, USB timing or DMA
hardware is emulated. The vendor save body executes; custom restore instructions
are replaced only by their register/stack ABI.
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
from unicorn.riscv_const import *
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'bare/drivers/src/usb_enum_recovered.c'
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
UDE, BUF, EPS, DATA, STOP = 0x30000, 0x31000, 0x32000, 0x40000, 0x70000
HOOKS = ('usb_interrupt_disable', 'memcpy', 'delay_us', 'usb_int_ep_reset',
         'usb_set_cur_ep', 'os_mq_ude_ep0_setup_post', 'os_mq_ude_reset_post',
         'os_mq_ude_ctl_flow_post')
API = ('usbd_ep0_transmit', 'get_setup', 'usbd_ep0_set_address', 'usbd_ep_reset',
       'ude_control_flow', 'usbd_ep0_send_status', 'usbd_ep_set_stall',
       'usbd_ep_clear_stall', 'ude_run_loop_execute', 'ude_init')


def member(name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib/libhal.a'), name])


def build(linker, tmp):
    original, save, recovered = (tmp / n for n in ('original.o', 'save.o', 'recovered.o'))
    original.write_bytes(member('usb_device_enum.o'))
    save.write_bytes(member('save-restore.o'))
    subprocess.run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                    '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE), '-o', str(recovered)], check=True)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.usbdev.com) *(.vector) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x28000; .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) *(COMMON) } __global_pointer$ = 0x28800; }')
    result = []
    for obj in (original, recovered):
        elf = obj.with_suffix('.elf')
        definitions = {'ude_0': UDE, 'ep0_buf': BUF, 'eps': EPS}
        definitions.update({name: 0x60000 + i * 16 for i, name in enumerate(HOOKS)})
        subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'ude_init', '-o', str(elf), str(obj), str(save)] +
                       [f'--defsym={name}={value}' for name, value in definitions.items()], check=True)
        result.append(elf.read_bytes())
    return result


class Machine:
    def __init__(self, binary, state, registers, packet):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.api = {n: symbols.get_symbol_by_name(n)[0]['st_value'] for n in API}
        self.regs, self.trace = dict(registers), []
        self.uc.mem_write(UDE, state)
        self.uc.mem_write(BUF, packet + bytes(512 - len(packet)))
        self.uc.mem_write(DATA, bytes(range(256)) * 256)
        self.uc.mem_write(EPS, struct.pack('<9I', *([UDE] * 9)))
        for i, name in enumerate(HOOKS):
            address = 0x60000 + i * 16
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.callback, name, address, address)
        for n in (1, 2, 4):
            address = symbols.get_symbol_by_name(f'__riscv_restore_{n}')[0]['st_value']
            self.uc.hook_add(UC_HOOK_CODE, self.restore, n, address, address)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=0, end=0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=0, end=0xfff)

    def restore(self, uc, address, size, count):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        words = struct.unpack('<' + 'I' * (count + 1), uc.mem_read(sp, 4 * (count + 1)))
        for i, reg in enumerate((UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2, UC_RISCV_REG_S3)[:count]):
            uc.reg_write(reg, words[count - 1 - i])
        uc.reg_write(UC_RISCV_REG_RA, words[-1])
        uc.reg_write(UC_RISCV_REG_SP, sp + 4 * (count + 1))
        uc.reg_write(UC_RISCV_REG_PC, words[-1])

    def callback(self, uc, address, size, name):
        a0, a1, a2 = [uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2)]
        if name == 'memcpy':
            self.trace.append(('call', name, a0, a1, a2))
            uc.mem_write(a0, bytes(uc.mem_read(a1, a2)))
        elif name == 'usb_set_cur_ep':
            self.trace.append(('call', name, a0))
            previous = struct.unpack('<I', uc.mem_read(EPS, 4))[0]
            uc.mem_write(EPS, struct.pack('<I', a0))
            uc.reg_write(UC_RISCV_REG_A0, previous)
        elif name == 'usb_int_ep_reset':
            self.trace.append(('call', name))
        else:
            self.trace.append(('call', name, a0))
            if name == 'os_mq_ude_ep0_setup_post':
                self.trace.append(('setup', bytes(uc.mem_read(a0, 8)).hex()))
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4
        value = self.regs.get(address, 0)
        self.trace.append(('read', address, value))
        uc.mem_write(address, struct.pack('<I', value))

    def write(self, uc, access, address, size, value, _):
        assert size == 4
        self.regs[address] = value & 0xffffffff
        self.trace.append(('write', address, value & 0xffffffff))

    def call(self, name, *args):
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1), args):
            self.uc.reg_write(reg, value)
        self.uc.emu_start(self.api[name], STOP, count=100000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000, (name, 'stack imbalance')
        return self.uc.reg_read(UC_RISCV_REG_A0) if name == 'usbd_ep0_transmit' else None

    def memory(self):
        return [bytes(self.uc.mem_read(p, n)) for p, n in ((UDE, 40), (BUF, 512), (EPS, 36), (DATA, 65536))]


def state(**changes):
    blob = bytearray(40)
    struct.pack_into('<I', blob, 0, 0x200)
    struct.pack_into('<II', blob, 8, BUF, DATA)
    struct.pack_into('<H', blob, 20, 8)
    fields = {'remaining': (16, 'H'), 'max_packet': (20, 'H'), 'state': (24, 'B'),
              'address': (25, 'B'), 'configuration': (26, 'B'), 'mode': (27, 'B'), 'flags': (28, 'I')}
    for name, value in changes.items():
        offset, fmt = fields[name]
        struct.pack_into('<' + fmt, blob, offset, value)
    return bytes(blob)


def cases():
    packet = bytes((0x80, 6, 1, 0, 0, 0, 18, 0))
    for mode in range(16):
        yield f'init/reset mode {mode}', state(mode=mode, flags=0xffffffff), {0x300: 0xffffffff}, packet, [
            ('ude_init', UDE), ('usbd_ep_reset',)]
    for maximum in (0, 1, 8, 64):
        for length in (0, 1, 7, 8, 9, 63, 64, 65, 128, 255, 65535):
            yield f'transmit max {maximum} length {length}', state(max_packet=maximum), {}, packet, [
                ('usbd_ep0_transmit', DATA, length), ('ude_control_flow',)]
    for configured in (0, 1, 255):
        for address in (0, 1, 127, 128, 255):
            value = bytearray(state(configuration=configured, flags=0xa5a5a5a5))
            value[34] = address
            yield f'address {address} configured {configured}', bytes(value), {}, packet, [
                ('usbd_ep0_set_address',), ('ude_control_flow',)]
    for csr in range(32):
        for current in (0, 1, 2, 3):
            yield f'control CSR {csr} state {current}', state(state=current, remaining=9), {0x244: csr, 0x258: 8}, packet, [('ude_control_flow',)]
    for request_type in range(0, 256, 16):
        value = bytes((request_type, 6, 1, 2, 3, 4, 5, 6))
        yield f'setup type {request_type:02x}', state(), {0x244: 1, 0x258: 3}, value, [('ude_control_flow',)]
    for count in (0, 1, 8, 64):
        yield f'setup FIFO drain {count}', state(), {0x258: count}, packet, [('get_setup', UDE)]
    for bus in range(8):
        for mask in (0, 1, 2, 4, 7):
            for tx in (0, 1, 3):
                regs = {0x218: bus, 0x22c: mask, 0x208: tx, 0x21c: 1, 0x210: 0xffffffff, 0x224: 0xffffffff}
                yield f'IRQ bus {bus} mask {mask} tx {tx}', state(), regs, packet, [('ude_run_loop_execute', UDE)]
    rng = random.Random(0x555342)
    for n in range(40):
        regs = {a: rng.getrandbits(32) for a in range(0x200, 0x320, 4)}
        regs[0x258] = rng.randrange(16)
        yield f'random controls {n}', state(mode=rng.randrange(256), flags=rng.getrandbits(32)), regs, packet, [
            ('usbd_ep0_send_status',), ('usbd_ep_set_stall',), ('usbd_ep_clear_stall',),
            ('usbd_ep_reset',), ('ude_run_loop_execute', UDE)]
    # Exact packet multiples require a final zero-length status transition.
    for length in (8, 16, 17, 64):
        actions = [('usbd_ep0_transmit', DATA, length)]
        actions += [('usbd_ep0_send_status',), ('ude_control_flow',)] * (length // 8 + 2)
        yield f'packet sequence {length}', state(), {}, packet, actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-usb-enum-') as directory:
        binaries = build(args.linker, Path(directory))
    report = {'unicorn_version': unicorn.__version__,
              'original_object_sha256': hashlib.sha256(member('usb_device_enum.o')).hexdigest(),
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'model': 'Synthetic MMIO, memcpy, endpoint-selection and event hooks; restore ABI shim. No USB bus, descriptor dispatch, DMA completion or silicon timing.',
              'cases': []}
    for name, initial, regs, packet, actions in cases():
        machines = [Machine(binary, initial, regs, packet) for binary in binaries]
        for action in actions:
            results = [machine.call(*action) for machine in machines]
            assert results[0] == results[1], (name, action, 'return mismatch', results)
            assert machines[0].trace == machines[1].trace, (name, action, machines[0].trace, machines[1].trace)
            assert machines[0].regs == machines[1].regs, (name, action, 'register mismatch')
            assert machines[0].memory() == machines[1].memory(), (name, action, 'RAM mismatch')
        report['cases'].append({'name': name, 'actions': len(actions), 'trace_events': len(machines[0].trace), 'result': 'pass'})
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'usb-enum-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} USB enum cases passed; original vs recovered RV32')


if __name__ == '__main__':
    main()
