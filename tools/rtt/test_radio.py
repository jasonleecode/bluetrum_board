#!/usr/bin/env python3
"""Run the real radio application's state machine with device models + UBSan."""
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, PROFILES, base_config

with tempfile.TemporaryDirectory(prefix='ab32-radio-model-') as directory:
    temp = Path(directory)
    (temp/'rtconfig.h').write_text(base_config((ROOT/'rtt/bsp/rtconfig.h').read_text()))
    includes = ['rtt/include', 'rtt/components/finsh', 'rtt/components/drivers/include',
                'rtt/components/drivers/audio', 'rtt/bsp/app',
                'rtt/bsp/libraries/hal_drivers', 'rtt/bsp/board/ports/audio']
    command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
               '-Wno-cast-function-type',
               '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
               '-fsanitize=undefined', '-fno-sanitize-recover=all',
               *[f'-D{k}={v}' for k,v in PROFILES['radio'].items()],
               '-I'+str(temp), *['-I'+str(ROOT/p) for p in includes],
               str(ROOT/'tools/rtt/tests/radio.c'), '-Wl,--gc-sections', '-o', str(temp/'radio')]
    subprocess.run(command, check=True)
    subprocess.run([temp/'radio'], check=True)
