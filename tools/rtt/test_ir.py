#!/usr/bin/env python3
"""Test native IR receiver callbacks against modeled MMIO; no hardware access."""
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, PROFILES, base_config

with tempfile.TemporaryDirectory(prefix='ab32-ir-model-') as directory:
    temp = Path(directory)
    (temp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    includes = ['tools/rtt/tests/include', 'rtt/include', 'rtt/components/finsh',
                'rtt/components/drivers/include', 'rtt/bsp/libcpu/cpu',
                'rtt/bsp/libraries/hal_drivers',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    command = ['cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-cast-function-type',
               '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
               '-fsanitize=undefined', '-fno-sanitize-recover=all',
               *[f'-D{k}={v}' for k, v in PROFILES['ir'].items()],
               '-I'+str(temp), *['-I'+str(ROOT / p) for p in includes],
               str(ROOT / 'tools/rtt/tests/irrx.c'), '-Wl,--gc-sections', '-o', str(temp / 'ir')]
    subprocess.run(command, check=True)
    subprocess.run([temp / 'ir'], check=True)
    # Configuration conflicts must fail at compile time even without menuconfig.
    for flag, message in [('-DBSP_USING_TIM3', 'share IRQ 6'),
                          ('-DBSP_USING_T3_PWM', 'share IRQ 6'),
                          ('-DBSP_USING_SPI1 -DBSP_SPI1_MAPPING=4', 'SPI1 G4'),
                          ('-DBSP_USING_IRRX_SW', 'software IR decoder')]:
        result = subprocess.run(command + flag.split(), capture_output=True, text=True)
        assert result.returncode and message in result.stderr, flag
    print('PASS: IR compile-time Timer3/PWM/SPI1-G4/legacy-software conflicts')
