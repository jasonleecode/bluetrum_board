#!/usr/bin/env python3
"""Execute ext_init.o and its C replacement with the same modeled hardware.

Compare MMIO, fixed RAM stores, calls/arguments, state at each call and final
memory. ROM/external functions are hooks, not recovered implementations.
"""
import argparse
import hashlib
import io
import json
import random
import shutil
import subprocess
import tempfile
from pathlib import Path

import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import riscv_const as R
from elftools.elf.elffile import ELFFile
from test_bt_core_recovery import Machine as CoreMachine, AREGS, SREGS, STOP, STACK, member

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_ext_init_recovered.c'
STATE, FLAG, TABLE = 0x14000, 0x15000, 0x230000
REGS = (0x544, 0x8000, 0x800c, 0x8018, 0x8020, 0x80c0, 0x81b0)
MEMORY = {'.btmem.bb': (STATE, 36), '.btmem.bb.rf': (FLAG, 1),
          '.rodata.bb.tbl': (TABLE, 24)}
ROM = {address: f'rom_{address:x}' for address in
       (0x845ac, 0x845a8, 0x845c0, 0x845c4, 0x845cc, 0x845b8)}


def build(args, tmp):
    original = member('libbtctrl.a', 'ext_init.o')
    (tmp / 'original.o').write_bytes(original)
    (tmp / 'save.o').write_bytes(member('libhal.a', 'save-restore.o'))
    obj = ELFFile(io.BytesIO(original))
    symbols = obj.get_section_by_name('.symtab')
    apis = {s.name for s in symbols.iter_symbols()
            if s['st_info']['type'] == 'STT_FUNC' and s['st_info']['bind'] == 'STB_GLOBAL'}
    externals = [s.name for s in symbols.iter_symbols() if s['st_shndx'] == 'SHN_UNDEF'
                 and s.name and not s.name.startswith('__riscv_')]
    hooks = {0x180000 + i * 16: name for i, name in enumerate(externals)}
    compilers = [('clang', ['clang', '--target=riscv32-unknown-elf'])]
    if args.gcc: compilers.append(('gcc', [args.gcc, '-msave-restore']))
    for name, command in compilers:
        subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding',
                        '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'bare/drivers/inc'), '-c', str(SOURCE),
                        '-o', str(tmp / (name + '.o'))], check=True)
        recovered = ELFFile(io.BytesIO((tmp / (name + '.o')).read_bytes()))
        actual_symbols = recovered.get_section_by_name('.symtab')
        assert {s.name for s in actual_symbols.iter_symbols()
                if s['st_info']['type'] == 'STT_FUNC' and s['st_info']['bind'] == 'STB_GLOBAL'} == apis
        for api in apis:
            assert recovered.get_section(actual_symbols.get_symbol_by_name(api)[0]['st_shndx']).name == '.text'
        for section, (_, size) in MEMORY.items():
            actual, expected = recovered.get_section_by_name(section), obj.get_section_by_name(section)
            for field in ('sh_type', 'sh_addralign', 'sh_size'):
                assert actual[field] == expected[field], (name, section, field)
            assert actual['sh_size'] == size and actual.data() == expected.data(), section
        for data in ('ext_init_reserve_on_test', 'ext_init_do_of_weather'):
            actual, expected = actual_symbols.get_symbol_by_name(data)[0], symbols.get_symbol_by_name(data)[0]
            for field in ('st_size', 'st_value', 'st_info'):
                assert actual[field] == expected[field], (name, data, field)
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { ' + ' '.join(f'{section} 0x{address:x} : {{ *({section}) }}'
                      for section, (address, _) in MEMORY.items()) +
                      ' .text 0x200000 : { *(.text*) } .rodata 0x240000 : { *(.rodata*) } '
                      '.data : { *(.data*) *(.sdata*) } .bss : { *(.bss*) *(.sbss*) } '
                      '__global_pointer$ = 0x240800; }')
    binaries = {}
    for name in ['original', *[name for name, _ in compilers]]:
        elf = tmp / (name + '.elf')
        subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                        '-e', 'bt21_set_on_tennis', '-o', str(elf), str(tmp / (name + '.o')),
                        str(tmp / 'save.o'),
                        *[f'--defsym={symbol}={address}' for address, symbol in hooks.items()]], check=True)
        binaries[name] = elf.read_bytes()
    listing = subprocess.check_output(['llvm-objdump-10', '--mattr=+m', '-dr', str(tmp / 'original.o')], text=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'bt-ext-init.llvm.txt').write_text(listing.replace(str(tmp / 'original.o'), 'libbtctrl.a:ext_init.o'))
    return binaries, original, apis, {**hooks, **ROM}


class Machine:
    # Share only integer helpers and the original compiler's restore ABI shim.
    put32, get32, restore = CoreMachine.put32, CoreMachine.get32, CoreMachine.restore

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
        self.regs[0x8000] = config.get('control', self.regs[0x8000] & 0x3fffffff)
        self.regs[0x544] = config.get('irq', self.regs[0x544])
        for address, size in ((0x11000, 0x800), (STATE, 36), (FLAG, 1)):
            self.uc.mem_write(address, bytes(rng.randrange(256) for _ in range(size)))
        self.put32(STATE, 0x11190)
        self.uc.mem_write(0x1119c, bytes([config.get('flags', 0)]))
        self.bd_addr = bytes(config.get('address', [rng.randrange(256) for _ in range(6)]))
        self.trace, self.waits, self.blocked = [], {}, False
        self.clock_reads = 0
        for address, name in hooks.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        for low, high in ((0x544, 0x547), (0x8000, 0x81ff)):
            self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=low, end=high)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=low, end=high)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.fixed_write, begin=0x11000, end=0x117ff)
        for count in range(13):
            symbol = symbols.get_symbol_by_name(f'__riscv_restore_{count}')
            if symbol:
                address = symbol[0]['st_value']
                self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=address, end=address)

    def memory(self):
        return [bytes(self.uc.mem_read(address, size)).hex() for address, size in
                ((STATE, 36), (FLAG, 1), (0x11000, 0x800), (TABLE, 24))]

    def hook(self, uc, address, size, name):
        args = [uc.reg_read(reg) for reg in AREGS[:3]]
        a0, a1, a2 = args
        result, call_args = 0xdeadbeef, []
        if name in ('rom_845ac', 'rom_845a8', 'rom_845c4', 'memset'): call_args = args
        elif name == 'rom_845c0': call_args = args[:2]
        elif name == 'rom_845cc': call_args = [a0, bytes(uc.mem_read(a0, 24)).hex()]
        elif name == 'bt_get_local_bd_addr': call_args = ['address_buffer']
        elif name == 'ext_init_cause_within_lift':
            call_args = [bytes(uc.mem_read(a0, 6)).hex()]
            assert bytes(uc.mem_read(a0, 6)) == self.bd_addr[::-1]
        elif name == 'my_printf':
            fmt = bytes(uc.mem_read(a0, 64)).split(b'\0')[0].decode('ascii')
            call_args = [fmt, a1]
        self.trace.append(('call', name, call_args, self.memory()))
        if name == 'memset':
            assert args == [STATE, 0, 36]
            uc.mem_write(a0, bytes([a1 & 255]) * a2)
            result = a0
        elif name == 'bt_get_local_bd_addr': uc.mem_write(a0, self.bd_addr)
        elif name == 'ext_init_cause_within_lift':
            # ext_util.o copies six bytes into context offset 24; it does not
            # retain the caller's temporary buffer (see its memcpy relocation).
            uc.mem_write(STATE + 24, bytes(uc.mem_read(a0, 6)))
        elif name == 'rom_845b8':
            result = (self.config.get('clock', 0x12345678) + self.clock_reads * 0xabcdef) & 0xffffffff
            self.clock_reads += 1
        if self.config.get('changing'):
            # Callees may update shared RAM and unrelated interrupt bits.
            self.regs[0x544] ^= 0x41
            if name == 'rom_845a8': uc.mem_write(STATE, bytes(range(36)))
            elif name == 'ext_init_get_without_name': uc.mem_write(STATE + 12, bytes(range(24)))
            elif name == 'bb_take_about_press': self.regs[0x8000] ^= 0x2345
            elif name == 'my_printf' and call_args[0].startswith('set_tx'):
                self.put32(STATE, 0x11240)  # The pointer must be loaded after printf.
        for i, reg in enumerate(AREGS): uc.reg_write(reg, 0xa5a50000 + i)
        uc.reg_write(R.UC_RISCV_REG_A0, result)
        uc.reg_write(R.UC_RISCV_REG_PC, uc.reg_read(R.UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        if address == 0x544 and self.config.get('changing'):
            self.regs[address] ^= 0x81
        if address in self.waits:
            bit, polls, label = self.waits[address]
            polls += 1
            self.waits[address] = (bit, polls, label)
            if self.config.get('stuck') == label:
                if polls >= 12:
                    self.blocked = True
                    uc.emu_stop()
            elif polls > self.config.get('wait', 2):
                self.regs[address] &= ~bit
                del self.waits[address]
        self.trace.append(('read', address, size, self.regs[address]))
        self.put32(address, self.regs[address])

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in REGS, (address, size)
        old = self.regs[address]
        self.trace.append(('write', address, size, value))
        self.regs[address] = value
        # Model hardware self-clearing command bits, once a command is written.
        if address == 0x8000:
            for bit, label in ((0x80000000, 'reset31'), (0x40000000, 'reset30')):
                if value & bit and not old & bit: self.waits[address] = (bit, 0, label)
        elif address == 0x8020 and value & 0x80000000:
            self.waits[address] = (0x80000000, 0, 'clock')

    def fixed_write(self, uc, access, address, size, value, _):
        self.trace.append(('ram_write', address, size, value))

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

    def snapshot(self):
        return self.trace, self.regs, self.memory(), self.blocked


def cases():
    for seed in range(32):
        yield f'lifecycle-{seed}', dict(seed=seed, changing=bool(seed & 1)), [
            ('bt21_set_on_tennis',), ('ext_init_pass_within_equivalent', 1),
            ('ext_init_pass_within_equivalent', 0), ('ext_init_pick_of_edge',),
            ('bb_write_without_passenger',), ('bt21_set_on_tennis',)]
    for byte in range(256):
        for changing in (False, True):
            yield f'flags-{byte}-{changing}', dict(seed=byte, flags=byte, changing=changing), [
                ('ext_init_pass_within_equivalent', 0), ('ext_init_pass_within_equivalent', 0x100),
                ('ext_init_pass_within_equivalent', 0xffffffff), ('ext_init_pass_within_equivalent', 0)]
    for index, address in enumerate(([0] * 6, [255] * 6, [0, 1, 2, 3, 4, 5],
                                     *[[((1 << bit) >> (8 * i)) & 255 for i in range(6)] for bit in range(48)])):
        yield f'address-{index}', dict(address=address, seed=index), [('bt21_set_on_tennis',),
                                                                   ('ext_init_reserve_to_word', 0)]
    for value in (0, 1, 0x7fffffff, 0x80000000, 0xffffffff, *[1 << bit for bit in range(32)]):
        for wait in (0, 1, 5):
            yield f'clock-{value:x}-{wait}', dict(clock=value, wait=wait, changing=True), [
                ('ext_init_reserve_to_word', value), ('ext_init_pick_of_edge',)]
    for control in (0, 0x3fffffff, 0x80000000, 0x40000000, 0xffffffff):
        # Initial control bits are retained in init; these cases don't reset.
        yield f'control-{control:x}', dict(control=control), [('bt21_set_on_tennis',)]
    for irq in (0, 0x80, 0x100, 0x180, 0xffffffff):
        for stuck in ('reset31', 'reset30', 'clock'):
            yield f'stuck-{irq:x}-{stuck}', dict(irq=irq, stuck=stuck), [('ext_init_pick_of_edge',)]
    for stuck, api in (('reset31', 'bb_write_without_passenger'), ('reset30', 'bb_write_without_passenger'),
                       ('clock', 'ext_init_reserve_to_word')):
        yield f'direct-stuck-{stuck}', dict(stuck=stuck), [(api, 0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    parser.add_argument('--gcc', help='also compare project GCC output')
    args = parser.parse_args()
    if not args.linker: parser.error('supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-ext-init-') as directory:
        binaries, original, apis, hooks = build(args, Path(directory))
    report = {'original_object_sha256': hashlib.sha256(original).hexdigest(),
              'recovered_source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'unicorn_version': unicorn.__version__, 'compilers': list(binaries)[1:],
              'exports': sorted(apis),
              'model': 'RV32 original vs recovered C. ROM/external hooks and modeled MMIO; '
                       'original restore ABI shim. No ROM implementation, RF or silicon timing validation.',
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
                        if left != right: raise AssertionError((name, key, action, index, left, right))
                    raise AssertionError((name, key, action, 'trace lengths', len(expected[0]), len(actual[0]),
                                          'state equal', expected[1:] == actual[1:]))
            assert expected[3] == bool(config.get('stuck')), name
        report['cases'].append({'name': name, 'actions': len(actions), 'result': 'pass'})
    (OUT / 'bt-ext-init-differential.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} cases passed; {len(apis)} exports, original vs {report["compilers"]}')


if __name__ == '__main__':
    main()
