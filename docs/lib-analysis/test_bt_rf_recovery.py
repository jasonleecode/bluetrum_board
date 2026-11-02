#!/usr/bin/env python3
"""Original bb_rf.o vs recovered RV32 C; modeled MMIO, external math hooks.

No radio or silicon timing claim. Delay loops execute their NOPs. The original
vendor restore instructions use the shared ABI shim. Weak overrides are tested
as hook calls, while default weak functions execute their actual instructions.
"""
import argparse
import hashlib
import io
import itertools
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
from test_bt_core_recovery import Machine as CoreMachine, AREGS, SREGS, STOP, STACK, member

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_rf_recovered.c'
TABLE, POINTER, OUTPUT, PROFILE, TRIM = 0x240000, 0x15000, 0x16000, 0x17000, 0x18000
RETURNS = {'bt_get_hvbko', 'bt_get_packet', 'bb_rf_cause_under_truck', 'bb_rf_fetch_within_fix',
           'bb_rf_reserve_at_world', 'bb_rf_find_inside_human', 'bb_rf_give_by_picture',
           'bb_rf_make_of_position', 'bb_rf_set_on_inflation', 'bb_rf_show_by_line',
           'bb_rf_sell_by_patience', 'bb_rf_tell_on_ease'}
MATH = ('bb_rf_fetch_without_dish', 'bb_rf_make_in_job', 'bb_rf_make_for_branch')
WEAK = {'bb_rf_show_by_line', 'bb_rf_sell_by_patience', 'bb_rf_send_by_burn'}


def build(args, tmp):
    original = member('libbtctrl.a', 'bb_rf.o')
    (tmp / 'original.o').write_bytes(original)
    (tmp / 'save.o').write_bytes(member('libhal.a', 'save-restore.o'))
    obj = ELFFile(io.BytesIO(original))
    syms = obj.get_section_by_name('.symtab')
    functions = {s.name: (obj.get_section(s['st_shndx']).name, s['st_info']['bind'])
                 for s in syms.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'}
    externals = [s.name for s in syms.iter_symbols() if s['st_shndx'] == 'SHN_UNDEF'
                 and s.name and s.name != 'bb_rf_read_inside_upper' and not s.name.startswith('__riscv_')]
    if args.real_math:
        externals = [name for name in externals if name not in MATH]
        (tmp / 'original-math.o').write_bytes(member('libbtctrl.a', 'complex.o'))
    hooks = {0x190000 + i * 16: name for i, name in enumerate(externals)}
    compilers = [('clang', ['clang', '--target=riscv32-unknown-elf'])]
    if args.gcc: compilers.append(('gcc', [args.gcc, '-msave-restore']))
    for name, command in compilers:
        subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding', '-fno-builtin',
                        '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'bare/drivers/inc'),
                        '-c', str(SOURCE), '-o', str(tmp / (name + '.o'))], check=True)
        if args.real_math:
            subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding', '-fno-builtin',
                            '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'bare/drivers/inc'),
                            '-c', str(ROOT / 'bare/drivers/src/bt_complex_recovered.c'),
                            '-o', str(tmp / (name + '-math.o'))], check=True)
        actual = ELFFile(io.BytesIO((tmp / (name + '.o')).read_bytes()))
        symbols = actual.get_section_by_name('.symtab')
        assert {s.name for s in symbols.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'
                and s['st_info']['bind'] != 'STB_LOCAL'} == {f for f, (_, bind) in functions.items() if bind != 'STB_LOCAL'}
        for f, (section, bind) in functions.items():
            symbol = symbols.get_symbol_by_name(f)
            assert symbol, (name, f, 'missing local or export')
            assert symbol[0]['st_info']['bind'] == bind, (name, f, 'binding')
            assert actual.get_section(symbol[0]['st_shndx']).name == section, (name, f, 'section')
        for section in ('.rodata', '.sdata'):
            left, right = actual.get_section_by_name(section), obj.get_section_by_name(section)
            for field in ('sh_size', 'sh_type', 'sh_addralign'):
                assert left[field] == right[field], (name, section, field, left[field], right[field])
            if section == '.rodata': assert left.data() == right.data()
    sections = list(dict.fromkeys(section for section, _ in functions.values()))
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { .sdata 0x15000 : { *(.sdata) } ' +
                      ' '.join(f'{section} 0x{0x200000 + i * 0x4000:x} : {{ *({section}) }}'
                               for i, section in enumerate(sections)) +
                      ' .rodata 0x240000 : { *(.rodata*) } .data : { *(.data*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x15800; }')
    binaries = {}
    for name in ['original', *[name for name, _ in compilers]]:
        elf = tmp / (name + '.elf')
        subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script), '-e', 'bb_give_into_radio',
                        '-o', str(elf), str(tmp / (name + '.o')), str(tmp / 'save.o'),
                        *([str(tmp / (name + '-math.o'))] if args.real_math else []),
                        f'--defsym=bb_rf_read_inside_upper={TRIM}',
                        *[f'--defsym={symbol}={address}' for address, symbol in hooks.items()]], check=True)
        binaries[name] = elf.read_bytes()
    listing = subprocess.check_output(['llvm-objdump-10', '--mattr=+m', '-dr', str(tmp / 'original.o')], text=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'bt-rf.llvm.txt').write_text(listing.replace(str(tmp / 'original.o'), 'libbtctrl.a:bb_rf.o'))
    return binaries, original, functions, hooks


class Machine:
    put32, get32, restore = CoreMachine.put32, CoreMachine.get32, CoreMachine.restore

    def __init__(self, binary, functions, hooks, config):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x400000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        syms = elf.get_section_by_name('.symtab')
        self.api = {f: syms.get_symbol_by_name(f)[0]['st_value'] for f in functions}
        assert self.get32(POINTER) == TABLE + 68
        rng = random.Random(config.get('seed', 0))
        self.config = config
        self.regs = {a: rng.getrandbits(32) for lo, hi in ((0x3f8, 0x3fc), (0x540, 0x544),
                     (0x7c00, 0x8200), (0xf000, 0xf100)) for a in range(lo, hi, 4)}
        self.regs[0x8070] &= ~3
        self.regs[0x7c7c] &= ~0x800000
        self.uc.mem_write(0x11000, bytes(rng.randrange(256) for _ in range(0x2000)))
        self.uc.mem_write(OUTPUT, bytes(rng.randrange(256) for _ in range(64)))
        self.uc.mem_write(PROFILE, bytes(config.get('profile', [1, 0, 4, 7, 3, 55, 4, 2, 4, 0])))
        self.uc.mem_write(TRIM, bytes([config.get('trim', 0)]))
        if 'flags' in config: self.uc.mem_write(0x111a4 + config.get('index', 0) * 88, bytes([config['flags']]))
        self.trace, self.waits = [], {}
        self.blocked, self.sample_index, self.math_index = False, 0, 0
        for address, name in hooks.items():
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name, begin=address, end=address)
        for low, high in ((0, 0xfff), (0x7c00, 0x81ff), (0xf000, 0xf0ff)):
            self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=low, end=high)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write, begin=low, end=high)
        for low, high in ((0x11000, 0x12fff), (OUTPUT, OUTPUT+63), (TRIM, TRIM)):
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.ram_write, begin=low, end=high)
        for count in range(13):
            symbols = syms.get_symbol_by_name(f'__riscv_restore_{count}')
            if symbols:
                a = symbols[0]['st_value']
                self.uc.hook_add(UC_HOOK_CODE, self.restore, count, begin=a, end=a)
        a = self.api['bb_rf_give_at_moment']
        self.uc.hook_add(UC_HOOK_CODE, self.delay, begin=a, end=a)
        if config.get('real_math'):
            for f in MATH:
                a = syms.get_symbol_by_name(f)[0]['st_value']
                self.uc.hook_add(UC_HOOK_CODE, self.observe_math, f, begin=a, end=a)
        if config.get('override'):
            for f in WEAK:
                a = self.api[f]
                self.uc.hook_add(UC_HOOK_CODE, self.hook, f, begin=a, end=a)

    def observe_math(self, uc, address, size, name):
        args = [uc.reg_read(reg) for reg in AREGS[:4]]
        self.trace.append(('math', name, args))

    def delay(self, uc, address, size, _):
        self.trace.append(('delay', uc.reg_read(AREGS[0])))

    def hook(self, uc, address, size, name):
        args = [uc.reg_read(reg) for reg in AREGS[:4]]
        a, b, c, d = args
        result, second, call_args = 0xdeadbeef, 0xa5a50001, []
        if name in ('bb_rf_fetch_without_dish', 'bb_rf_make_in_job', 'bb_rf_make_for_branch'): call_args = args
        elif name == 'bb_rf_send_by_burn': call_args = [a]
        self.trace.append(('call', name, call_args, self.get32(POINTER), bytes(uc.mem_read(TRIM, 1)).hex()))
        if name == 'bb_rf_reserve_into_strategy': result = 0 if self.config.get('null') else PROFILE
        elif name == 'bb_rf_get_about_importance': result = self.config.get('importance', 0)
        elif name == 'bb_rf_fetch_without_dish': result, second = (a-c) & 0xffffffff, (b-d) & 0xffffffff
        elif name == 'bb_rf_make_in_job': result, second = (a*c-b*d) & 0xffffffff, (a*d+b*c) & 0xffffffff
        elif name == 'bb_rf_make_for_branch':
            # Deliberately inject signed/boundary outputs, independently of the
            # real complex division. This verifies the caller's encoding only.
            values = self.config.get('corrections', [0, 1, -1, 127, -128, 128, 255, 256, -257,
                                                    0x7fffffff, 0x80000000, 42, -42, 129, -129, 511, -511, 3])
            result, second = values[(2*self.math_index) % len(values)], values[(2*self.math_index+1) % len(values)]
            self.math_index += 1
        elif name == 'bb_rf_show_by_line': result = self.config.get('line', 0x105)
        elif name == 'bb_rf_sell_by_patience': result = self.config.get('limit', 0x100)
        if self.config.get('changing'): self.regs[0x540] ^= 0x81
        for i, reg in enumerate(AREGS): uc.reg_write(reg, 0xa5a50000 + i)
        uc.reg_write(AREGS[0], result & 0xffffffff)
        uc.reg_write(AREGS[1], second & 0xffffffff)
        uc.reg_write(R.UC_RISCV_REG_PC, uc.reg_read(R.UC_RISCV_REG_RA))

    def read(self, uc, access, address, size, value, _):
        assert size == 4 and address in self.regs, (hex(address), size)
        if address == 0x540 and self.config.get('changing'): self.regs[address] ^= 0x101
        if address in self.waits:
            bit, polls, label = self.waits[address]
            polls += 1
            self.waits[address] = (bit, polls, label)
            if self.config.get('stuck') == label:
                if polls >= 12:
                    self.blocked = True
                    uc.emu_stop()
            elif polls > self.config.get('wait', 2):
                self.regs[address] |= bit
                del self.waits[address]
        if address == 0x7da4:
            samples = self.config.get('samples', [0, 127, 128, 0xfffff, 0x100000, 0x1fffff, 0xffffffff,
                                                 0x80000000, 0x7fffffff, 0x100001, 0x1fff80])
            self.regs[address] = samples[self.sample_index % len(samples)]
            self.sample_index += 1
        self.trace.append(('read', address, size, self.regs[address]))
        self.put32(address, self.regs[address])

    def write(self, uc, access, address, size, value, _):
        assert size == 4 and address in self.regs, (hex(address), size)
        self.trace.append(('write', address, size, value))
        self.regs[address] = value
        pending = None
        if address == 0x8070 and value & 1: pending = (0x8070, 2, 'bus')
        elif address == 0x7c84 and value & 0x8000: pending = (0x7c7c, 0x800000, 'channel')
        elif address == 0x7d7c and value == 1: pending = (0x7d78, 0x800000, 'sample')
        if pending:
            target, bit, label = pending
            self.regs[target] &= ~bit
            self.waits[target] = (bit, 0, label)

    def ram_write(self, uc, access, address, size, value, _):
        self.trace.append(('ram_write', address, size, value))

    def call(self, name, *args):
        self.uc.reg_write(R.UC_RISCV_REG_SP, STACK)
        self.uc.reg_write(R.UC_RISCV_REG_RA, STOP)
        for i, reg in enumerate(SREGS): self.uc.reg_write(reg, 0x12345600+i)
        for i, reg in enumerate(AREGS): self.uc.reg_write(reg, args[i] if i < len(args) else 0xa5a50000+i)
        self.uc.emu_start(self.api[name], STOP, count=500000)
        if self.blocked:
            self.trace.append(('blocked', name))
            return
        assert self.uc.reg_read(R.UC_RISCV_REG_PC) == STOP, name
        assert self.uc.reg_read(R.UC_RISCV_REG_SP) == STACK, name
        assert [self.uc.reg_read(r) for r in SREGS] == [0x12345600+i for i in range(12)], name
        if name in RETURNS: self.trace.append(('return', name, self.uc.reg_read(AREGS[0])))

    def snapshot(self):
        return self.trace, self.regs, self.get32(POINTER), self.blocked, [bytes(self.uc.mem_read(a, n)) for a, n in
                    ((0x11000, 0x2000), (OUTPUT, 64), (PROFILE, 10), (TRIM, 1), (TABLE, 89))]


def cases():
    for seed in range(20):
        yield f'lifecycle-{seed}', dict(seed=seed, null=bool(seed&1), importance=seed&2, trim=seed,
                                       changing=bool(seed&4), wait=seed%5), [
            ('bt_get_hvbko', 0x81), ('bb_make_into_mother',), ('bb_show_with_stay', OUTPUT),
            ('bb_pass_about_dependent', OUTPUT), ('bb_set_about_agent',), ('bb_sell_about_bet',)]
    for value in range(256):
        yield f'profile-{value}', dict(seed=value, trim=value, null=True), [
            ('bt_get_packet', value), ('bt_get_hvbko', value), ('bb_rf_pass_on_water', OUTPUT),
            ('bb_rf_show_by_line',), ('bb_rf_sell_by_patience',), ('bb_rf_set_on_inflation', value),
            ('bb_rf_get_with_leadership', 0), ('bb_rf_get_at_application', value)]
        for mode in (0, 1, 2, 3):
            index = value % 20
            yield f'counter-{value}-{mode}', dict(flags=value, seed=value, index=index), [
                ('bb_rf_give_by_picture', index, mode), ('bb_rf_reserve_at_world', index),
                ('bb_rf_find_inside_human', index)]
    for seed, bits in enumerate(itertools.product((0, 1, 0x100), repeat=3)):
        yield f'switch-{seed}', dict(seed=seed), [('bb_rf_show_on_cash', *bits),
              ('bb_rf_keep_about_uncle', *bits), ('bb_rf_send_for_confidence', *bits[:2])]
    for value in (0, 1, 127, 128, 255, 256, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff):
        for gain in (0, 15, 16, 96, 111, 112, 127, 255, 256, 0xffffffff):
            yield f'rssi-{value:x}-{gain:x}', {}, [('bb_rf_fetch_within_fix', value, gain)]
        yield f'parameters-{value:x}', dict(seed=value, override=True, changing=True, limit=value,
                                           line=value, trim=value&255), [
            ('bt_get_packet', value), ('bt_get_hvbko', value), ('bb_rf_get_at_application', value),
            ('bb_rf_reserve_into_mud', value), ('bb_rf_take_without_start',),
            ('bb_rf_get_with_leadership', PROFILE+2), ('bb_rf_set_on_inflation', value),
            ('bb_rf_cause_under_truck', value), ('bb_rf_tell_at_entry', value, value),
            ('bb_rf_pass_without_consequence', value, value), ('bb_rf_read_into_price', value),
            ('bb_rf_send_to_officer',), ('bb_rf_go_inside_log',), ('bb_rf_make_of_position',),
            ('bb_rf_send_by_burn', value)]
    for i, sample in enumerate((0, 1, 127, 128, 255, 0xfffff, 0x100000, 0x100001,
                                0x1fffff, 0x80000000, 0xffffffff)):
        yield f'sample-{i}', dict(samples=[sample], wait=i%4), [('bb_rf_tell_on_ease', i%2, 24, i)]
        yield f'calibration-{i}', dict(samples=[sample], profile=[i&1,0,255,128,255,0,3,7,255,0],
                                      trim=i, importance=i, wait=i%3), [('bb_make_into_mother',)]
    for label, action in (('bus', ('bb_rf_cause_under_truck', 1)), ('bus', ('bb_rf_tell_at_entry', 1, 2)),
                           ('sample', ('bb_give_into_radio',)), ('channel', ('bb_rf_read_into_price', 10)),
                           ('channel', ('bb_rf_pass_without_consequence', 10, 1))):
        yield f'stuck-{action[0]}', dict(stuck=label), [action]
    for count in (0, 1, 6, 35, 1000, 0xffffffff, 0x80000000):
        yield f'delay-{count:x}', {}, [('bb_rf_give_at_moment', count)]
    yield 'local-sequences', {}, [('bb_rf_keep_about_professor',), ('bb_rf_pick_to_test',),
                                ('bb_rf_show_in_worker',), ('bb_rf_get_into_temporary',)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    parser.add_argument('--gcc')
    parser.add_argument('--real-math', action='store_true',
                        help='link and execute original/recovered complex.o in calibration cases')
    args = parser.parse_args()
    if not args.linker: parser.error('provide --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-rf-') as directory:
        binaries, original, functions, hooks = build(args, Path(directory))
    report = dict(original_object_sha256=hashlib.sha256(original).hexdigest(),
                  recovered_source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  unicorn_version=unicorn.__version__, compilers=list(binaries)[1:],
                  exports=sorted(f for f, (_, bind) in functions.items() if bind != 'STB_LOCAL'),
                  local_functions=sorted(f for f, (_, bind) in functions.items() if bind == 'STB_LOCAL'),
                  model='Modeled RF MMIO and external math hooks, original restore ABI shim; no hardware RF/timing validation.', cases=[])
    if args.real_math:
        report['math_original_sha256'] = hashlib.sha256(member('libbtctrl.a', 'complex.o')).hexdigest()
        report['math_source_sha256'] = hashlib.sha256((ROOT / 'bare/drivers/src/bt_complex_recovered.c').read_bytes()).hexdigest()
        report['model'] = 'RF MMIO/platform hooks; actual original/recovered complex arithmetic executes with no math hooks. No silicon timing validation.'
    covered = set()
    for name, config, actions in cases():
        if args.real_math and not name.startswith(('lifecycle-', 'calibration-')):
            continue
        config['real_math'] = args.real_math
        machines = {key: Machine(binary, functions, hooks, config) for key, binary in binaries.items()}
        for action in actions:
            covered.add(action[0])
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
        report['cases'].append(dict(name=name, actions=len(actions), result='pass'))
    if not args.real_math:
        assert covered == set(functions), set(functions)-covered
    filename = 'bt-rf-complex-differential.json' if args.real_math else 'bt-rf-differential.json'
    (OUT / filename).write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(report["cases"])} groups passed; {len(report["exports"])} exports + '
          f'{len(report["local_functions"])} local functions; original vs {report["compilers"]}')


if __name__ == '__main__': main()
