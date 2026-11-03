#!/usr/bin/env python3
"""Compile optional BSP drivers in a disposable copy; never edits user config."""
import argparse
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    'default': {},
    'peripherals': dict(RT_USING_SPI=1, BSP_USING_SPI1=1, BSP_SPI1_MAPPING=4,
        BSP_SPI1_TIMEOUT_MS=100, RT_USING_WDT=1, BSP_USING_WDT=1,
        RT_USING_RTC=1, BSP_USING_ONCHIP_RTC=1, RTC_USING_INTERNAL_CLK=1,
        RT_USING_ADC=1, BSP_USING_ADC=1, BSP_USING_ADC0=1,
        RT_USING_HWTIMER=1, BSP_USING_TIM=1,
        **{f'BSP_USING_TIM{i}': 1 for i in range(1, 6)}),
    'audio': dict(RT_USING_AUDIO=1, BSP_USING_AUDIO=1, BSP_USING_AUDIO_PLAY=1,
        RT_AUDIO_REPLAY_MP_BLOCK_COUNT=2, RT_AUDIO_REPLAY_MP_BLOCK_SIZE=4096,
        RT_AUDIO_RECORD_PIPE_SIZE=2048),
    'pwm': dict(RT_USING_PWM=1, BSP_USING_PWM=1,
        BSP_USING_T3_PWM=1, BSP_USING_T3_PWM0=1,
        BSP_USING_T4_PWM=1, BSP_USING_T4_PWM1=1,
        BSP_USING_T5_PWM=1, BSP_USING_T5_PWM0=1,
        BSP_USING_LPWM0=1, BSP_USING_LPWM0_G1=1,
        BSP_USING_LPWM1=1, BSP_USING_LPWM1_G3=1,
        BSP_USING_LPWM2=1, BSP_USING_LPWM2_G2=1),
    'flash': dict(RT_USING_FAL=1, BSP_USING_ON_CHIP_FLASH=1, FAL_PART_HAS_TABLE_CFG=1),
    'ir': dict(BSP_USING_IRRX=1, BSP_USING_IRRX_HW=1),
    'sd': dict(RT_USING_SDIO=1, RT_USING_BLK=1, BSP_USING_SDIO=1,
        SDIO_MAX_FREQ=24000000, RT_SDIO_STACK_SIZE=1024,
        RT_SDIO_THREAD_PRIORITY=15, RT_MMCSD_STACK_SIZE=2048,
        RT_MMCSD_THREAD_PRIORITY=22, RT_MMCSD_MAX_PARTITION=16,
        BSP_USING_SDCARD=1, RT_USING_DFS=1, RT_USING_DFS_V1=1,
        DFS_USING_POSIX=1, DFS_USING_WORKDIR=1, DFS_FD_MAX=16,
        DFS_FILESYSTEMS_MAX=4, DFS_FILESYSTEM_TYPES_MAX=4,
        RT_USING_DFS_ELMFAT=1, RT_DFS_ELM_CODE_PAGE=437,
        RT_DFS_ELM_WORD_ACCESS=1, RT_DFS_ELM_USE_LFN_0=1,
        RT_DFS_ELM_USE_LFN=0, RT_DFS_ELM_LFN_UNICODE=0,
        RT_DFS_ELM_MAX_LFN=255, RT_DFS_ELM_DRIVES=2,
        RT_DFS_ELM_MAX_SECTOR_SIZE=512, RT_DFS_ELM_REENTRANT=1,
        RT_DFS_ELM_MUTEX_TIMEOUT=3000),
}
PROFILES['spi_fm'] = dict(PROFILES['audio'], BSP_USING_FMRX=1,
    RT_USING_SPI=1, BSP_USING_SPI1=1, BSP_SPI1_MAPPING=4, BSP_SPI1_TIMEOUT_MS=100)
PROFILES['spi_fm_i2s'] = dict(PROFILES['spi_fm'], BSP_USING_I2S=1, BSP_I2S_MAPPING=1)
PROFILES['i2s_pa'] = dict(PROFILES['audio'], BSP_USING_I2S=1, BSP_I2S_MAPPING=0)
PROFILES['bluetooth'] = dict(PROFILES['audio'], BSP_USING_FMRX=1, BSP_USING_BTCTRL=1,
    RT_AUDIO_USING_LAZY_INIT=1)
PROFILES['nimble'] = dict(PROFILES['bluetooth'], BSP_USING_NIMBLE=1)
PROFILES['usb'] = dict(PROFILES['nimble'], BSP_USING_USB_CDC=1)
PROFILES['tf'] = dict(PROFILES['usb'], BSP_USING_TFCARD=1,
    **{k: v for k, v in PROFILES['sd'].items() if k.startswith(('DFS_', 'RT_USING_DFS', 'RT_DFS_'))})
PROFILES['tf'].update(DFS_FD_MAX=4, DFS_FILESYSTEMS_MAX=1,
    DFS_FILESYSTEM_TYPES_MAX=1, RT_DFS_ELM_DRIVES=1)
PROFILES['ir_integrated'] = dict(PROFILES['tf'], **PROFILES['ir'])
PROFILES['keys'] = dict(PROFILES['ir_integrated'], BSP_USING_KEYS=1)
PROFILES['keys_irq'] = dict(PROFILES['keys'], BSP_USING_PIN_IRQ=1)
PROFILES['rtc'] = dict(RT_USING_RTC=1, BSP_USING_ONCHIP_RTC=1, RTC_USING_INTERNAL_CLK=1)
PROFILES['rtc_external'] = dict(RT_USING_RTC=1, BSP_USING_ONCHIP_RTC=1)
PROFILES['rtc_integrated'] = dict(PROFILES['keys'], **PROFILES['rtc'])
PROFILES['led'] = dict(PROFILES['rtc_integrated'], BSP_USING_LED_BREATH=1,
    BSP_USING_PWM=1, RT_USING_PWM=1, BSP_USING_T5_PWM=1, BSP_USING_T5_PWM0=1)
PROFILES['power'] = dict(PROFILES['rtc'], BSP_USING_POWER=1)
PROFILES['power_external'] = dict(PROFILES['rtc_external'], BSP_USING_POWER=1)
PROFILES['power_integrated'] = dict(PROFILES['led'], BSP_USING_POWER=1)
PROFILES['audec'] = dict(PROFILES['power'], BSP_USING_AUDEC=1)
PROFILES['audec_integrated'] = dict(PROFILES['power_integrated'], BSP_USING_AUDEC=1)
PROFILES['audec_fm'] = dict(PROFILES['audec'], **PROFILES['audio'], BSP_USING_FMRX=1)

def base_config(config):
    """Keep optional profiles independent of enabled workspace peripherals."""
    for key in set().union(*(p.keys() for p in PROFILES.values())):
        config = re.sub(r'^#define\s+' + key + r'\b[^\n]*', '', config, flags=re.M)
    return config

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('profiles', nargs='*', metavar='PROFILE',
                    help='one or more of: ' + ', '.join(PROFILES))
    args = ap.parse_args()
    if set(args.profiles) - set(PROFILES):
        ap.error('unknown profile: ' + ', '.join(set(args.profiles) - set(PROFILES)))
    work = Path(tempfile.mkdtemp(prefix='ab32-rtt-build-'))
    print(f'Build workspace/logs: {work}', flush=True)
    shutil.copytree(ROOT / 'rtt', work / 'rtt', ignore=shutil.ignore_patterns(
        'build', '*.o', '.git', '.sconsign.dblite', '__pycache__', 'rtthread.elf',
        'rtthread.bin', 'rtthread.map', 'cconfig.h'))
    (work / 'bare').mkdir()
    (work / 'bare/lib').symlink_to(ROOT / 'bare/lib', target_is_directory=True)
    shutil.copytree(ROOT / 'bare/drivers', work / 'bare/drivers',
                    ignore=shutil.ignore_patterns('*.o'))
    env = os.environ.copy()
    env.setdefault('RTT_EXEC_PATH', str(ROOT / '.toolchains' /
        'riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14/bin'))
    env.setdefault('RTT_CC_PREFIX', 'riscv64-unknown-elf-')
    config = (ROOT / 'rtt/bsp/rtconfig.h').read_text()
    failed = []
    for name in args.profiles or PROFILES:
        additions = ''.join(f'\n#undef {key}\n#define {key} {val}\n'
                            for key, val in PROFILES[name].items())
        profile_config = config if name == 'default' else base_config(config)
        (work / 'rtt/bsp/rtconfig.h').write_text(profile_config + additions)
        log = work / f'{name}.log'
        with log.open('w') as out:
            result = subprocess.run(['scons', '-j4'], cwd=work / 'rtt/bsp',
                                    env=env, stdout=out, stderr=subprocess.STDOUT)
        print(f'{name}: {"PASS" if result.returncode == 0 else "FAIL"} ({log})', flush=True)
        if result.returncode:
            failed.append(name)
            print('\n'.join(log.read_text().splitlines()[-22:]), flush=True)
        else:
            linkmap = (work / 'rtt/bsp/rtthread.map').read_text()
            if PROFILES[name].get('BSP_USING_AUDEC'):
                assert 'drv_audec.o' in linkmap and 'ab32_audec_init' in linkmap
                assert re.search(r'\.audec_work\s+0x0*54000\s+0x3000', linkmap)
                assert 'libcodecs.a' not in linkmap and 'libbtctrl.a' not in linkmap
            if PROFILES[name].get('BSP_USING_POWER') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_POWER\b', config, re.M)):
                assert 'ab32_power_init' in linkmap and 'ab32_irtc_transaction' in linkmap
            if PROFILES[name].get('BSP_USING_ONCHIP_RTC') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_ONCHIP_RTC\b', config, re.M)):
                assert '.irq.rtc' in linkmap and 'rt_hw_rtc_register' in linkmap
                assert not re.search(r'libhal\.a\([^)]*rtc[^)]*\)', linkmap), 'vendor RTC object linked'
            if PROFILES[name].get('BSP_USING_KEYS') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_KEYS\b', config, re.M)):
                assert 'board_keys_init' in linkmap and 'board_keys_poll' in linkmap
            if PROFILES[name].get('BSP_USING_IRRX') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_IRRX\b', config, re.M)):
                assert 'drv_irrx.o' in linkmap and 'ab32_irrx_init' in linkmap
                assert not re.search(r'libhal\.a\(ir[^)]*\)', linkmap), 'vendor IR object linked'
            if PROFILES[name].get('BSP_USING_TFCARD') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_TFCARD\b', config, re.M)):
                assert 'drv_sdcard.o' in linkmap and 'drv_tf.o' in linkmap
                assert 'drv_sdio.o' not in linkmap, 'both SD0 owners linked'
            if PROFILES[name].get('BSP_USING_FMRX'):
                for member in ('fmrx.o', 'fmrx_ang.o'):
                    assert f'libhal.a({member})' not in linkmap, 'FM vendor object linked'
                assert 'fmrx_analog_recovered.o' in linkmap
                assert 'fmrx_digital_recovered.o' in linkmap
            if PROFILES[name].get('BSP_USING_I2S'):
                assert 'i2s_src_recovered.o' in linkmap
            if PROFILES[name].get('BSP_USING_BTCTRL') or (name == 'default' and
                    re.search(r'^#define\s+BSP_USING_BTCTRL\b', config, re.M)):
                assert 'libbtctrl.a' not in linkmap, 'vendor Bluetooth archive linked'
                assert 'bt_core_recovered.o' in linkmap
                if PROFILES[name].get('BSP_USING_USB_CDC') or (name == 'default' and
                        re.search(r'^#define\s+BSP_USING_USB_CDC\b', config, re.M)):
                    assert 'usb_dc_ab32.o' in linkmap
                    assert not re.search(r'libhal\.a\(usb[^)]*\)', linkmap), 'vendor USB object linked'
                assert re.search(r'0x0*11000\s+bb_get_without_vehicle\b', linkmap)
                from memory_report import inspect
                import json
                report = inspect(work / 'rtt/bsp/rtthread.elf')
                (work / f'{name}-memory.json').write_text(json.dumps(report, indent=2) + '\n')
            shutil.copy2(work / 'rtt/bsp/rtthread.elf', work / f'{name}.elf')
            shutil.copy2(work / 'rtt/bsp/rtthread.map', work / f'{name}.map')
    return bool(failed)

if __name__ == '__main__':
    raise SystemExit(main())
