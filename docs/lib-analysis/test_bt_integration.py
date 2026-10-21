#!/usr/bin/env python3
"""Check Bluetooth wrapper regressions and partial recovery archive selection.

The RV32 link is isolated, with an artificial layout; it is not bootable firmware.
Requires host cc, clang, make, readelf and a RISC-V-capable ld.lld.
"""
import argparse
import itertools
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'bare/drivers/inc'
SRC = ROOT / 'bare/drivers/src'
RECOVERED = ('bt_hct_recovered', 'bt_platform_recovered')
PLATFORM_API = ('baseband_platform_pass_by_hello', 'bthw_irq_init', 'bthw_isr_do_plus')


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    for spi, fm, bt in itertools.product(('vendor', 'recovered'), repeat=3):
        result = run(['make', '-s', '-C', ROOT / 'bare', 'info',
                      f'SPI_BACKEND={spi}', f'FM_BACKEND={fm}', f'BT_SUPPORT_BACKEND={bt}'],
                     capture_output=True, text=True).stdout
        for name in RECOVERED:
            assert (name + '.o' in result) == (bt == 'recovered')
        build = 'build' + ('/recovered-spi' if spi == 'recovered' else '')
        build += '/recovered-fm' if fm == 'recovered' else ''
        build += '/recovered-bt-support' if bt == 'recovered' else ''
        assert f'{build}/startup.o' in result
    invalid = subprocess.run(['make', '-s', '-C', str(ROOT / 'bare'), 'info',
                              'BT_SUPPORT_BACKEND=invalid'], capture_output=True, text=True)
    assert invalid.returncode != 0 and 'BT_SUPPORT_BACKEND must be' in invalid.stderr
    print('PASS Makefile: eight backend combinations and invalid selector', flush=True)

    with tempfile.TemporaryDirectory(prefix='ab32-bt-integration-') as directory:
        tmp = Path(directory)
        for actual_shim in (False, True):
            command = ['cc', '-std=c11', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                       '-I', INC, ROOT / 'docs/lib-analysis/tests/bt_wrapper_test.c',
                       SRC / 'drv_bluetooth.c', '-o', tmp / 'wrapper']
            if actual_shim:
                command += ['-DUSE_VENDOR_SHIM', SRC / 'vendor_shim.c']
            run(command)
            run([tmp / 'wrapper'])

        objects = {}
        for name in ('drv_bluetooth', 'vendor_shim', *RECOVERED):
            objects[name] = tmp / (name + '.o')
            run(['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                 '-ffreestanding', '-fno-builtin', '-Os', '-Wall', '-Wextra', '-Werror',
                 '-I', INC, '-c', SRC / (name + '.c'), '-o', objects[name]])
        for recovered in (False, True):
            selected = ['drv_bluetooth', 'vendor_shim'] + (list(RECOVERED) if recovered else [])
            elf = tmp / ('recovered.elf' if recovered else 'vendor.elf')
            linked = run([args.linker, '-m', 'elf32lriscv', '--no-relax', '--trace',
                          '-Ttext=0x200000', '-e', 'drv_bt_init', '-o', elf,
                          *[objects[name] for name in selected], '--start-group',
                          ROOT / 'bare/lib/libbtctrl.a', ROOT / 'bare/lib/libhal.a', '--end-group'],
                         capture_output=True, text=True)
            for member in ('hct_msg.o', 'hct_proc.o'):
                assert (f'libbtctrl.a({member})' in linked.stdout) != recovered
            # These dependencies deliberately remain; recovery is partial.
            for member in ('hct.o', 'bb.o', 'bb_rf.o', 'baseband_platform.o', 'platform_asm.o'):
                assert f'libbtctrl.a({member})' in linked.stdout
            symbols = run(['readelf', '-Ws', elf], capture_output=True, text=True).stdout
            for name in PLATFORM_API:
                matches = [line.split() for line in symbols.splitlines()
                           if line.split() and line.split()[-1] == name]
                assert len(matches) == 1 and matches[0][6] != 'UND', name
                assert matches[0][4] == ('GLOBAL' if recovered else 'WEAK'), name
            assert not [line for line in symbols.splitlines()
                        if ' UND ' in line and line.split()[-1] != 'UND']
            print(f'PASS isolated RV32 link: {"recovered support" if recovered else "vendor"}; '
                  'archive members and platform bindings verified')


if __name__ == '__main__':
    main()
