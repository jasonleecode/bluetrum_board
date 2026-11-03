#!/usr/bin/env python3
"""Compile SPI pin-sharing combinations; never accesses the board."""
from pathlib import Path
import subprocess
import tempfile
from check_builds import ROOT, base_config


def main():
    includes = ['tools/rtt/tests/include', 'rtt/include', 'rtt/components/finsh',
                'rtt/components/drivers/include', 'rtt/components/drivers/include/drivers',
                'rtt/bsp/libraries/hal_drivers',
                'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    cases = [
        ('G6', {}, None),
        ('G4 with I2S PE', dict(BSP_SPI1_MAPPING=4, BSP_USING_I2S=1, BSP_I2S_MAPPING=1), None),
        ('G6 with I2S PA', dict(BSP_USING_I2S=1, BSP_I2S_MAPPING=0), None),
        ('UART0', dict(BSP_SPI1_MAPPING=2), 'conflicts with UART0'),
        ('UART1', dict(BSP_SPI1_MAPPING=1, BSP_USING_UART1=1), 'conflicts with UART1'),
        ('UART2', dict(BSP_SPI1_MAPPING=3, BSP_USING_UART2=1), 'conflicts with UART2'),
        ('I2S', dict(BSP_USING_I2S=1, BSP_I2S_MAPPING=1), 'conflicts with I2S'),
        ('I2S default', dict(BSP_USING_I2S=1), 'conflicts with I2S'),
        ('IIC header', dict(BSP_USING_I2C1=1, BSP_I2C1_SCL_PIN=16,
                            BSP_I2C1_SDA_PIN=15), 'cannot own the same pins'),
        ('invalid group', dict(BSP_SPI1_MAPPING=7), 'mapping must be 1..6'),
    ]
    with tempfile.TemporaryDirectory(prefix='ab32-spi-config-') as directory:
        config = base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text())
        (Path(directory) / 'rtconfig.h').write_text(config)
        for name, extra, error in cases:
            defines = dict(RT_USING_SPI=1, BSP_USING_SPI1=1, BSP_SPI1_MAPPING=6,
                           BSP_SPI1_TIMEOUT_MS=100)
            defines.update(extra)
            result = subprocess.run(['cc', '-std=c11', '-fsyntax-only',
                *[f'-D{k}={v}' for k, v in defines.items()], '-I' + directory,
                *['-I' + str(ROOT / p) for p in includes],
                str(ROOT / 'rtt/bsp/libraries/hal_drivers/drv_spi.c')],
                text=True, capture_output=True)
            if error:
                assert result.returncode and error in result.stderr, (name, result.stderr)
            else:
                assert result.returncode == 0, (name, result.stderr)
            print('PASS:', name)


if __name__ == '__main__':
    main()
