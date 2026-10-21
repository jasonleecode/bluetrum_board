#!/usr/bin/env python3
"""Validate native SD0 with a scripted MMIO/card model and isolated RV32 link.

No card contents are read/written. This is not a silicon or archive differential
verification; controller semantics come from the checked-in register manual.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'bare/drivers/src'
INC = ROOT / 'bare/drivers/inc'
SOURCES = [
    'bare/drivers/src/drv_sdcard.c', 'bare/drivers/inc/drv_sdcard.h',
    'docs/lib-analysis/tests/sdcard_test.c', 'docs/lib-analysis/test_sdcard.py',
    'rtt/bsp/libraries/hal_drivers/drv_sdio.c',
    'rtt/bsp/libraries/hal_drivers/drv_sdio.h',
    'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/source/ab32vg1_hal_sd.c',
    'rtt/components/drivers/sdio/dev_sd.c',
    'rtt/bsp/board/ab32vg1_hal_msp.c',
    'docs/ab32vg1_doc/AB32VG1_Register.pdf',
    'docs/ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf',
]


def run(args):
    return subprocess.run([str(x) for x in args], check=True, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('provide --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-sdcard-') as tmpdir:
        tmp = Path(tmpdir)
        run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             '-DDRV_SDCARD_TEST', '-I', INC, SRC / 'drv_sdcard.c',
             ROOT / 'docs/lib-analysis/tests/sdcard_test.c', '-o', tmp / 'model'])
        output = run([tmp / 'model']).stdout.strip()
        print(output)
        match = re.search(r'(\d+) scenarios, (\d+) assertions', output)
        assert match
        flags = ['clang', '--target=riscv32-unknown-elf', '-march=rv32imac',
                 '-mabi=ilp32', '-std=c11', '-Os', '-ffreestanding', '-fno-builtin',
                 '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
                 '-Werror', '-I', INC]
        for name in ('drv_sdcard', 'vendor_shim'):
            run([*flags, '-c', SRC / (name + '.c'), '-o', tmp / (name + '.o')])
        exports = ('init', 'deinit', 'get_info', 'last_error', 'read_block', 'write_block')
        run([args.linker, '-m', 'elf32lriscv', '--no-relax', '--gc-sections',
             '-Ttext=0x200000', '-e', 'drv_sdcard_init',
             *('--undefined=drv_sdcard_' + name for name in exports),
             tmp / 'drv_sdcard.o', tmp / 'vendor_shim.o', '-o', tmp / 'sd.elf'])
        symbols = run(['readelf', '-Ws', tmp / 'sd.elf']).stdout
        for name in exports:
            assert 'drv_sdcard_' + name in symbols
        unresolved = [line for line in symbols.splitlines()
                      if ' UND ' in line and line.split()[-1] != 'UND']
        assert not unresolved, unresolved
        assert not re.search(r'\b(?:blue_spi|drv_spi|sdio_send_cmd)', symbols)
        print('PASS: RV32 compile/link, all 6 APIs retained, no archive/SPI dependency')
        report = {
            'kind': 'scripted MMIO/card model plus isolated RV32 compile/link',
            'scenarios': int(match[1]), 'assertions': int(match[2]),
            'rv32_link': 'all six APIs, vendor_shim memory helpers only; no vendor archives',
            'limits': ['No SD card or silicon execution',
                       'Not an archive differential test: no SD card object in libhal.a',
                       'Artificial link layout, not a bootable firmware',
                       'DMA completion, CRC bits, card responses supplied by test model',
                       'Real timer, DMA memory placement, power and pins require board validation'],
            'sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                       for name in SOURCES},
        }
        destination = ROOT / 'docs/lib-analysis/generated/recovery/sdcard-validation.json'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
