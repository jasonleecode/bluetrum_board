#!/usr/bin/env python3
"""USB wrapper regressions, backend selection and isolated RV32 links.

This uses an artificial link layout; it is not a bootable/USB-enumerated image.
Requires host cc, clang, make, readelf and a RISC-V-capable ld.lld.
"""
import argparse
import itertools
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'bare/drivers/inc'
SRC = ROOT / 'bare/drivers/src'
RECOVERED = ('usb_comm_recovered', 'usb_ep_recovered', 'usb_enum_recovered', 'usb_class_recovered')
MEMBERS = ('usb_comm.o', 'usb_detect.o', 'usb_device_ep.o', 'usb_device_enum.o', 'usb_device_hid.o')


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def exported_functions(obj):
    headers = run(['readelf', '-SW', obj], capture_output=True, text=True).stdout
    sections = {}
    for line in headers.splitlines():
        match = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)', line)
        if match:
            sections[match[1]] = match[2]
    symbols = run(['readelf', '-Ws', obj], capture_output=True, text=True).stdout
    result = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[3] == 'FUNC' and fields[4] in ('GLOBAL', 'WEAK') and fields[6] != 'UND':
            result[fields[7]] = (fields[4], sections[fields[6]])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    for spi, fm, bt, usb in itertools.product(('vendor', 'recovered'), repeat=4):
        result = run(['make', '-s', '-C', ROOT / 'bare', 'info',
                      f'SPI_BACKEND={spi}', f'FM_BACKEND={fm}', f'BT_SUPPORT_BACKEND={bt}',
                      f'USB_BACKEND={usb}'], capture_output=True, text=True).stdout
        for name in RECOVERED:
            assert (name + '.o' in result) == (usb == 'recovered')
        build = 'build' + ('/recovered-spi' if spi == 'recovered' else '')
        build += '/recovered-fm' if fm == 'recovered' else ''
        build += '/recovered-bt-support' if bt == 'recovered' else ''
        build += '/recovered-usb' if usb == 'recovered' else ''
        assert f'{build}/startup.o' in result
    invalid = subprocess.run(['make', '-s', '-C', str(ROOT / 'bare'), 'info',
                              'USB_BACKEND=invalid'], capture_output=True, text=True)
    assert invalid.returncode != 0 and 'USB_BACKEND must be' in invalid.stderr
    print('PASS Makefile: 16 backend combinations and invalid selector', flush=True)

    with tempfile.TemporaryDirectory(prefix='ab32-usb-integration-') as directory:
        tmp = Path(directory)
        for actual_shim in (False, True):
            # Non-PIE keeps static data in the 32-bit range: endpoint ABI stores
            # a uint32 DMA address, even in this host-only mock test.
            command = ['cc', '-std=c11', '-no-pie', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                       '-I', INC, ROOT / 'docs/lib-analysis/tests/usb_wrapper_test.c',
                       SRC / 'drv_usb.c', SRC / 'drv_usb_audio.c', SRC / 'drv_audio.c',
                       '-o', tmp / 'wrapper']
            if actual_shim:
                command += ['-DUSE_VENDOR_SHIM', SRC / 'vendor_shim.c']
            run(command)
            run([tmp / 'wrapper'])
        objects = {}
        flags = ['clang', '--target=riscv32-unknown-elf', '-march=rv32imac', '-mabi=ilp32',
                 '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
                 '-Os', '-Wall', '-Wextra', '-Werror', '-I', INC]
        for name in ('drv_usb', 'drv_usb_audio', 'vendor_shim', *RECOVERED):
            objects[name] = tmp / (name + '.o')
            run([*flags, '-c', SRC / (name + '.c'), '-o', objects[name]])
        groups = dict(zip(RECOVERED, [(MEMBERS[0], MEMBERS[1]), (MEMBERS[2],),
                                      (MEMBERS[3],), (MEMBERS[4],)]))
        for source, members in groups.items():
            expected = {}
            for member in members:
                obj = tmp / member
                obj.write_bytes(run(['ar', 'p', ROOT / 'bare/lib/libhal.a', member], capture_output=True).stdout)
                expected.update(exported_functions(obj))
            assert exported_functions(objects[source]) == expected, source
        print('PASS USB exported APIs: original function names, bindings and code sections preserved')
        run([*flags, '-fsyntax-only', ROOT / 'bare/app/usb_fm.c', SRC / 'drv_audio.c'])
        for recovered in (False, True):
            selected = ['drv_usb', 'drv_usb_audio', 'vendor_shim'] + (list(RECOVERED) if recovered else [])
            elf = tmp / ('recovered.elf' if recovered else 'vendor.elf')
            # Same dead-code elimination as the project. The unused legacy
            # usb_device_hid_send requires missing ude_hid_send and is discarded.
            linked = run([args.linker, '-m', 'elf32lriscv', '--no-relax', '--gc-sections', '--trace',
                          '-Ttext=0x200000', '-e', 'drv_usb_init', '-u', 'drv_usb_run_loop',
                          '-u', 'drv_usb_hid_try_send', '-u', 'drv_usb_stop', '-o', elf,
                          *[objects[name] for name in selected], '--start-group',
                          ROOT / 'bare/lib/libhal.a', ROOT / 'bare/lib/libbtctrl.a', '--end-group'],
                         capture_output=True, text=True)
            for member in MEMBERS:
                assert (f'libhal.a({member})' in linked.stdout) != recovered
            assert 'libhal.a(usb_device_run_loop.o)' in linked.stdout
            symbols = run(['readelf', '-Ws', elf], capture_output=True, text=True).stdout
            for name in ('usb_init', 'usb_device_enter', 'usb_ep_start_transfer',
                         'usb_ep_transfer', 'ude_run_loop_execute', 'udh_init'):
                matches = [line.split() for line in symbols.splitlines()
                           if line.split() and line.split()[-1] == name]
                assert len(matches) == 1 and matches[0][6] != 'UND', name
            assert not [line for line in symbols.splitlines()
                        if ' UND ' in line and line.split()[-1] != 'UND']
            print(f'PASS isolated RV32 USB link: {"recovered" if recovered else "vendor"}; '
                  'five archive replacements verified')


if __name__ == '__main__':
    main()
