#!/usr/bin/env python3
"""Test real AUDEC control flow with modeled MMIO; no physical MP3 computation."""
from pathlib import Path
import subprocess
import tempfile
from check_builds import ROOT, base_config

with tempfile.TemporaryDirectory(prefix='ab32-audec-model-') as directory:
    p=Path(directory)
    config=base_config((ROOT/'rtt/bsp/rtconfig.h').read_text())
    (p/'rtconfig.h').write_text(config+'\n#define BSP_USING_AUDEC\n')
    includes=['tools/rtt/tests/audec_board','rtt/include','rtt/components/drivers/include',
              'rtt/components/finsh','rtt/bsp/libraries/hal_libraries/bmsis/include',
              'rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include']
    cmd=['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-no-pie',
         '-ffunction-sections','-fdata-sections','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(p),*['-I'+str(ROOT/i) for i in includes],
         str(ROOT/'tools/rtt/tests/audec.c'),'-Wl,--gc-sections,--section-start=.audec.work=0x54000',
         '-o',str(p/'test')]
    subprocess.run(cmd,check=True)
    subprocess.run([p/'test'],check=True)
    invalid_layout=[x.replace(',--section-start=.audec.work=0x54000','') for x in cmd]
    subprocess.run(invalid_layout+['-DBAD_AUDEC_LAYOUT'],check=True)
    subprocess.run([p/'test'],check=True)
    result=subprocess.run(cmd+['-DBSP_USING_BTCTRL'],capture_output=True,text=True)
    assert result.returncode and 'AUDEC work RAM overlaps' in result.stderr
    print('PASS: AUDEC/BT compile-time memory conflict guard')
