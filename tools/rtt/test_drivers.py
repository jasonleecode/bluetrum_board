#!/usr/bin/env python3
"""Run BSP callbacks with real RT-Thread headers and modeled MMIO (no hardware)."""
from pathlib import Path
import subprocess
import tempfile
from check_builds import ROOT, PROFILES

with tempfile.TemporaryDirectory(prefix='ab32-rtt-model-') as directory:
    temp = Path(directory)
    defines = [f'-D{k}={v}' for k, v in PROFILES['peripherals'].items()]
    includes = ['tools/rtt/tests/include', 'rtt/bsp', 'rtt/include', 'rtt/components/finsh', 'rtt/components/drivers/audio',
                'rtt/components/drivers/include', 'rtt/components/drivers/include/drivers',
                'rtt/bsp/libraries/hal_drivers/config', 'rtt/components/fal/inc',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include',
                'rtt/bsp/board/ports', 'rtt/bsp/libcpu/cpu', 'bare/drivers/inc',
                'rtt/components/dfs/dfs_v1/include', 'rtt/bsp/libraries/hal_drivers']
    for name, profile in [('peripherals', PROFILES['peripherals']), ('sd', PROFILES['sd']),
                          ('fm', PROFILES['spi_fm_i2s']),
                          ('audio_rtc_flash', dict(PROFILES['audio'], **PROFILES['flash'],
                            RT_USING_RTC=1, BSP_USING_ONCHIP_RTC=1, RTC_USING_INTERNAL_CLK=1))]:
        defines = [f'-D{k}={v}' for k, v in profile.items()]
        subprocess.run(['cc', '-std=c11', '-O2', '-g', '-ffunction-sections',
                        '-fdata-sections', '-Werror=incompatible-pointer-types',
                        *defines, *['-I'+str(ROOT / p) for p in includes],
                        str(ROOT / f'tools/rtt/tests/{name}.c'), '-Wl,--gc-sections',
                        '-o', str(temp / name)], check=True)
        subprocess.run([temp / name], check=True)
