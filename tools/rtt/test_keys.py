#!/usr/bin/env python3
"""Exercise actual GPIO/HAL and board-key code with modeled MMIO; no board access."""
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, base_config

with tempfile.TemporaryDirectory(prefix='ab32-keys-model-') as directory:
    temp = Path(directory)
    (temp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    includes = ['tools/rtt/tests/gpio_board', 'rtt/include', 'rtt/components/finsh',
                'rtt/components/drivers/include', 'rtt/bsp/libcpu/cpu', 'rtt/bsp/app',
                'rtt/bsp/libraries/hal_drivers',
                'rtt/bsp/libraries/hal_libraries/bmsis/include',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
               '-Wno-unused-parameter', '-Wno-cast-function-type',
               '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
               '-fsanitize=undefined', '-fno-sanitize-recover=all',
               '-DBSP_USING_KEYS=1', '-DBSP_USING_PIN_IRQ=1',
               '-I'+str(temp), *['-I'+str(ROOT / p) for p in includes],
               str(ROOT / 'tools/rtt/tests/keys_gpio.c'), '-Wl,--gc-sections', '-o', str(temp / 'keys')]
    subprocess.run(command, check=True)
    subprocess.run([temp / 'keys'], check=True)
    for flags, message in [('-DBSP_USING_SPI1 -DBSP_SPI1_MAPPING=1', 'SPI1 G1'),
                           ('-DBSP_USING_SPI1 -DBSP_SPI1_MAPPING=5', 'SPI1 G1'),
                           ('-DBSP_USING_I2S -DBSP_I2S_MAPPING=0', 'I2S G1'),
                           ('-DBSP_USING_LPWM2_G3', 'LPWM2 G3')]:
        result = subprocess.run(command + flags.split(), capture_output=True, text=True)
        assert result.returncode and message in result.stderr, flags
    print('PASS: key pin conflicts with SPI1 G1/G5, I2S G1 and LPWM2 G3 rejected')
