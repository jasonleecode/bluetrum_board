#!/usr/bin/env python3
"""RTC bridge/state tests with real chip definitions and modeled MMIO; no board I/O."""
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, PROFILES, base_config

with tempfile.TemporaryDirectory(prefix='ab32-rtc-model-') as directory:
    temp = Path(directory)
    (temp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    includes = ['tools/rtt/tests/rtc_board', 'rtt/include', 'rtt/components/finsh',
                'rtt/components/drivers/include',
                'rtt/bsp/libraries/hal_libraries/bmsis/include',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    for profile in ('rtc', 'rtc_external', 'power', 'power_external'):
        command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-cast-function-type', '-Werror=return-type',
                   '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
                   '-fsanitize=undefined', '-fno-sanitize-recover=all',
                   *[f'-D{k}={v}' for k,v in PROFILES[profile].items()],
                   '-I'+str(temp), *['-I'+str(ROOT/p) for p in includes],
                   str(ROOT/'tools/rtt/tests/rtc.c'), '-Wl,--gc-sections', '-o', str(temp/'rtc')]
        subprocess.run(command, check=True)
        subprocess.run([temp/'rtc'], check=True)
        print(f'PASS: {profile}')
    # The shell command must allow first setting without a readable old date.
    command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
               '-Wno-unused-parameter', '-Wno-cast-function-type', '-Werror=return-type',
               '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
               '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DRT_USING_RTC=1',
               '-I'+str(temp), *['-I'+str(ROOT/p) for p in includes],
               str(ROOT/'tools/rtt/tests/date.c'), '-Wl,--gc-sections', '-o', str(temp/'date')]
    subprocess.run(command, check=True)
    subprocess.run([temp/'date'], check=True)
