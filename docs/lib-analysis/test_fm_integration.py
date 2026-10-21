#!/usr/bin/env python3
"""Check the FM wrapper contract, Makefile selection and isolated RV32 link.

No board is accessed. The RV32 link stubs hal_mdelay and is not a firmware build.
Requires host cc, clang, readelf, make and a RISC-V-capable ld.lld.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'bare/drivers/inc'
SRC = ROOT / 'bare/drivers/src'
RECOVERED = ('fmrx_analog_recovered', 'fmrx_digital_recovered')
API = ('power_on', 'power_off', 'digital_start', 'digital_stop', 'dac_sync',
       'dma_to_aubuf', 'get_audio_data', 'analog_init', 'set_pll', 'set_rf_cap')


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    for spi in ('vendor', 'recovered'):
        for fm in ('vendor', 'recovered'):
            result = run(['make', '-s', '-C', ROOT / 'bare', 'info',
                          f'SPI_BACKEND={spi}', f'FM_BACKEND={fm}'],
                         capture_output=True, text=True).stdout
            for source in RECOVERED:
                assert (source + '.o' in result) == (fm == 'recovered')
            build = 'build' + ('/recovered-spi' if spi == 'recovered' else '')
            build += '/recovered-fm' if fm == 'recovered' else ''
            assert f'{build}/startup.o' in result
    invalid = subprocess.run(['make', '-s', '-C', str(ROOT / 'bare'), 'info',
                              'FM_BACKEND=invalid'], capture_output=True, text=True)
    assert invalid.returncode != 0 and 'FM_BACKEND must be' in invalid.stderr
    print('PASS Makefile: four backend combinations and invalid selector')

    with tempfile.TemporaryDirectory(prefix='ab32-fm-integration-') as directory:
        tmp = Path(directory)
        run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', INC,
             ROOT / 'docs/lib-analysis/tests/fm_wrapper_test.c', SRC / 'drv_fm.c',
             '-o', tmp / 'wrapper'])
        run([tmp / 'wrapper'])
        flags = ['clang', '--target=riscv32-unknown-elf', '-march=rv32imac',
                 '-mabi=ilp32', '-ffreestanding', '-fno-builtin', '-Os',
                 '-Wall', '-Wextra', '-Werror', '-I', INC]
        objects = []
        for name in ('drv_fm', *RECOVERED):
            obj = tmp / (name + '.o')
            run([*flags, '-c', SRC / (name + '.c'), '-o', obj])
            objects.append(obj)
        for name in ('drv_audio', 'drv_usb_audio'):
            run([*flags, '-fsyntax-only', SRC / (name + '.c')])
        script = tmp / 'link.ld'
        script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) } '
                          '. = 0x20000; .rodata : { *(.rodata*) } '
                          '. = 0x30000; .data : { *(.data*) *(.sdata*) } '
                          '.bss : { *(.bss*) *(.sbss*) } }')
        linked = run([args.linker, '-m', 'elf32lriscv', '--no-relax', '--trace',
                      '-T', script, '-e', 'drv_fm_init', '-o', tmp / 'fm.elf',
                      *objects, ROOT / 'bare/lib/libhal.a', '--defsym=hal_mdelay=0x60000'],
                     capture_output=True, text=True)
        assert 'fmrx.o' not in linked.stdout and 'fmrx_ang.o' not in linked.stdout
        symbols = run(['readelf', '-Ws', tmp / 'fm.elf'],
                      capture_output=True, text=True).stdout
        for name in API:
            matches = [line.split() for line in symbols.splitlines()
                       if line.split() and line.split()[-1] == 'fmrx_' + name]
            assert len(matches) == 1 and matches[0][6] != 'UND', name
        assert not [line for line in symbols.splitlines()
                    if ' UND ' in line and line.split()[-1] != 'UND']
        print('PASS RV32 link: wrapper + 10 recovered APIs; vendor FM objects unused')


if __name__ == '__main__':
    main()
