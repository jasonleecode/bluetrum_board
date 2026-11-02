#!/usr/bin/env python3
"""Offline TF adapter/FatFs and native SD0 model tests; never accesses hardware."""
from pathlib import Path
import subprocess
import tempfile
from check_builds import ROOT, PROFILES, base_config

with tempfile.TemporaryDirectory(prefix='ab32-tf-test-') as directory:
    temp = Path(directory)
    (temp / 'rtconfig.h').write_text(base_config((ROOT / 'rtt/bsp/rtconfig.h').read_text()))
    # Use RTT's directory ABI while keeping the host libc for other headers.
    (temp / 'dirent.h').write_bytes((ROOT / 'rtt/components/libc/compilers/common/include/dirent.h').read_bytes())
    includes = ['rtt/include', 'rtt/components/finsh', 'bare/drivers/inc',
                'rtt/components/drivers/include', 'rtt/components/dfs/dfs_v1/include',
                'rtt/components/dfs/dfs_v1/filesystems/elmfat']
    defines = [f'-D{k}={v}' for k, v in PROFILES['tf'].items()
               if k.startswith(('DFS_', 'RT_USING_DFS', 'RT_DFS_')) or k == 'BSP_USING_TFCARD']
    flags = ['cc', '-std=c11', '-D_DEFAULT_SOURCE',
             '-O2', '-g', '-ffunction-sections', '-fdata-sections',
             '-Werror=incompatible-pointer-types', '-fsanitize=undefined',
             '-fno-sanitize-recover=all']
    subprocess.run([*flags, *defines, '-I'+str(temp),
        *['-I'+str(ROOT / p) for p in includes],
        str(ROOT / 'tools/rtt/tests/tf.c'),
        str(ROOT / 'rtt/components/dfs/dfs_v1/filesystems/elmfat/ff.c'),
        '-Wl,--gc-sections', '-o', str(temp / 'tf')], check=True)
    subprocess.run([temp / 'tf'], check=True)
    subprocess.run([*flags, '-Wall', '-Wextra', '-Werror', '-DDRV_SDCARD_TEST',
        '-I'+str(ROOT / 'bare/drivers/inc'), str(ROOT / 'bare/drivers/src/drv_sdcard.c'),
        str(ROOT / 'docs/lib-analysis/tests/sdcard_test.c'), '-o', str(temp / 'sd')], check=True)
    subprocess.run([temp / 'sd'], check=True)
