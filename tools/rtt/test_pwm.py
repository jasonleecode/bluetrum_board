#!/usr/bin/env python3
"""Test actual PWM/HAL/keys/breathing code against modeled MMIO; no board access."""
import subprocess
import tempfile
from pathlib import Path
from check_builds import ROOT, PROFILES, base_config

with tempfile.TemporaryDirectory(prefix='ab32-pwm-model-') as directory:
    temp = Path(directory)
    (temp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    profile = dict(PROFILES['pwm'], BSP_USING_KEYS=1, BSP_USING_LED_BREATH=1,
                   BSP_USING_T5_PWM1=1, BSP_USING_T5_PWM2=1, BSP_USING_LPWM3=1)
    includes = ['tools/rtt/tests/pwm_board', 'rtt/include', 'rtt/components/finsh',
                'rtt/components/drivers/include', 'rtt/bsp/libcpu/cpu', 'rtt/bsp/app',
                'rtt/bsp/libraries/hal_drivers/config',
                'rtt/bsp/libraries/hal_libraries/bmsis/include',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
               '-Wno-unused-parameter', '-Wno-cast-function-type',
               '-Werror=incompatible-pointer-types', '-ffunction-sections', '-fdata-sections',
               '-fsanitize=undefined', '-fno-sanitize-recover=all',
               *[f'-D{k}={v}' for k, v in profile.items()],
               '-I'+str(temp), *['-I'+str(ROOT / p) for p in includes],
               str(ROOT / 'tools/rtt/tests/pwm_led.c'), '-Wl,--gc-sections', '-o', str(temp / 'pwm')]
    subprocess.run(command, check=True)
    subprocess.run([temp / 'pwm'], check=True)
    for flags, message in [('-DBSP_USING_TIM5', 'already owned'),
                           ('-DBSP_USING_I2S -DBSP_I2S_MAPPING=1', 'I2S PE1'),
                           ('-DBSP_USING_IRRX', 'already owned'),
                           ('-DBSP_USING_TFCARD', 'TF/SD PB0')]:
        result = subprocess.run(command + flags.split(), capture_output=True, text=True)
        assert result.returncode and message in result.stderr, flags
    print('PASS: PWM timer/pin ownership conflict guards')
