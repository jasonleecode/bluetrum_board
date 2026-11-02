#!/usr/bin/env python3
"""Exercise the real USB core, CDC class and AB32 port against modeled MMIO."""
from pathlib import Path
import subprocess
import tempfile
from check_builds import ROOT, base_config

with tempfile.TemporaryDirectory(prefix='ab32-usb-model-') as directory:
    tmp = Path(directory)
    (tmp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    stack = ROOT / 'rtt/components/drivers/usb/cherryusb'
    includes = [tmp, ROOT / 'rtt/include', ROOT / 'rtt/components/finsh',
                ROOT / 'rtt/bsp/board/ports/usb', stack / 'core', stack / 'common', stack / 'class/cdc']
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                    '-Werror=incompatible-pointer-types', *['-I'+str(p) for p in includes],
                    str(ROOT / 'tools/rtt/tests/usb_cdc.c'), str(stack / 'core/usbd_core.c'),
                    str(stack / 'class/cdc/usbd_cdc_acm.c'), '-Wl,--gc-sections',
                    '-o', str(tmp / 'usb_cdc')], check=True)
    subprocess.run([tmp / 'usb_cdc'], check=True)
