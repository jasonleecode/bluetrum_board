#!/usr/bin/env python3
"""Compare all complex.o exports against recovered Clang/GCC RV32 code.

The original and replacement execute their arithmetic instructions directly;
there are no callee or math hooks. Inputs include overflow and zero divisors.
"""
import argparse
import hashlib
import io
import itertools
import json
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_MEM_WRITE
from unicorn import riscv_const as R
from elftools.elf.elffile import ELFFile
from test_bt_core_recovery import AREGS, SREGS, member

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
SOURCE = ROOT / 'bare/drivers/src/bt_complex_recovered.c'
MASK, STACK, STOP = 0xffffffff, 0x30000, 0x1f000


def build(args, tmp):
    original = member('libbtctrl.a', 'complex.o')
    (tmp/'original.o').write_bytes(original)
    obj = ELFFile(io.BytesIO(original))
    apis = {s.name for s in obj.get_section_by_name('.symtab').iter_symbols()
            if s['st_info']['type'] == 'STT_FUNC' and s['st_info']['bind'] == 'STB_GLOBAL'}
    compilers = [('clang', ['clang', '--target=riscv32-unknown-elf'])]
    if args.gcc: compilers.append(('gcc', [args.gcc]))
    for name, command in compilers:
        subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding', '-fno-builtin',
                        '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'bare/drivers/inc'),
                        '-c', str(SOURCE), '-o', str(tmp/(name+'.o'))], check=True)
        recovered = ELFFile(io.BytesIO((tmp/(name+'.o')).read_bytes()))
        symbols = recovered.get_section_by_name('.symtab')
        assert {s.name for s in symbols.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'
                and s['st_info']['bind'] == 'STB_GLOBAL'} == apis
        assert not [s.name for s in symbols.iter_symbols() if s.name and s['st_shndx'] == 'SHN_UNDEF']
        for api in apis:
            assert recovered.get_section(symbols.get_symbol_by_name(api)[0]['st_shndx']).name == '.text'
    binaries = {}
    for name in ['original', *[name for name, _ in compilers]]:
        elf = tmp/(name+'.elf')
        subprocess.run([args.linker, '-m', 'elf32lriscv', '--no-relax', '-Ttext=0x10000',
                        '-e', 'complex_set_into_brave', '-o', str(elf), str(tmp/(name+'.o'))], check=True)
        binaries[name] = elf.read_bytes()
    OUT.mkdir(parents=True, exist_ok=True)
    listing = subprocess.check_output(['llvm-objdump-10', '--mattr=+m', '-dr', str(tmp/'original.o')], text=True)
    (OUT/'bt-complex.llvm.txt').write_text(listing.replace(str(tmp/'original.o'), 'libbtctrl.a:complex.o'))
    return original, apis, binaries


class Machine:
    def __init__(self, binary, apis):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x40000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        symbols = elf.get_section_by_name('.symtab')
        self.apis = {api: symbols.get_symbol_by_name(api)[0]['st_value'] for api in apis}
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write)

    def write(self, uc, access, address, size, value, _):
        assert STACK-256 <= address and address+size <= STACK, (address, size)

    def call(self, api, inputs):
        uc = self.uc
        uc.reg_write(R.UC_RISCV_REG_SP, STACK)
        uc.reg_write(R.UC_RISCV_REG_RA, STOP)
        for i, reg in enumerate(SREGS): uc.reg_write(reg, 0x12345600+i)
        for i, reg in enumerate(AREGS): uc.reg_write(reg, inputs[i] if i < 4 else 0xabc00000+i)
        uc.emu_start(self.apis[api], STOP, count=1000)
        assert uc.reg_read(R.UC_RISCV_REG_PC) == STOP, api
        assert uc.reg_read(R.UC_RISCV_REG_SP) == STACK, api
        assert [uc.reg_read(r) for r in SREGS] == [0x12345600+i for i in range(12)], api
        return uc.reg_read(AREGS[0]), uc.reg_read(AREGS[1])


def signed(n):
    n &= MASK
    return n if n < 0x80000000 else n-0x100000000


def oracle(api, values):
    a, b, c, d = values
    if api == 'complex_set_into_brave': result = a+c, b+d
    elif api == 'bb_rf_fetch_without_dish': result = a-c, b-d
    elif api == 'bb_rf_make_in_job': result = a*c-b*d, a*d+b*c
    elif not (c or d): result = 0, 0
    else:
        denominator = signed(c*c+d*d)
        half = abs(denominator//2)
        result = []
        for n in (signed(a*c+b*d), signed(b*c-a*d)):
            adjusted = signed(n-half if n < 0 else n+half)
            if not denominator: result.append(-1)
            else:
                quotient = abs(adjusted)//abs(denominator)
                result.append(-quotient if (adjusted < 0) != (denominator < 0) else quotient)
    return tuple(n & MASK for n in result)


def cases():
    for i, values in enumerate(itertools.product(range(-2, 3), repeat=4)):
        yield f'small-{i}', tuple(v & MASK for v in values)
    pairs = [(0,0), (1,0), (-1,0), (0,1), (0,-1), (1,1), (-1,-1), (2,3), (-2,3), (3,-2),
             (127,128), (255,256), (32767,32768), (32768,32768), (46340,0), (46341,0),
             (46341,1), (65535,0), (65536,0), (0,65536), (65536,65536), (0x80000000,0),
             (0x7fffffff,0), (0xffffffff,0xffffffff), (0x80000000,0x80000000), (0x7fffffff,1)]
    for i, (a, b) in enumerate(itertools.product(pairs, repeat=2)):
        yield f'boundary-{i}', tuple(v & MASK for v in (*a, *b))
    rng = random.Random(0x5301)
    for i in range(2048):
        yield f'random-{i}', tuple(rng.getrandbits(32) for _ in range(4))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    parser.add_argument('--gcc')
    args = parser.parse_args()
    if not args.linker: parser.error('provide --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-complex-') as directory:
        original, apis, binaries = build(args, Path(directory))
    machines = {name: Machine(binary, apis) for name, binary in binaries.items()}
    report = dict(original_object_sha256=hashlib.sha256(original).hexdigest(),
                  recovered_source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  unicorn_version=unicorn.__version__, compilers=list(binaries)[1:], exports=sorted(apis),
                  model='Direct original/recovered RV32 arithmetic execution plus independent integer oracle; no hooks.', cases=[])
    for name, inputs in cases():
        for api in sorted(apis):
            expected = machines['original'].call(api, inputs)
            assert expected == oracle(api, inputs), (name, api, inputs, expected, oracle(api, inputs))
            for compiler, machine in machines.items():
                actual = machine.call(api, inputs)
                assert actual == expected, (name, api, compiler, inputs, expected, actual)
        report['cases'].append(dict(name=name, actions=4, result='pass'))
    (OUT/'bt-complex-differential.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(report["cases"])} groups passed; four exports, original vs {report["compilers"]}; no math hooks')


if __name__ == '__main__': main()
