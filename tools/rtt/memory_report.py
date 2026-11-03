#!/usr/bin/env python3
"""Report the unified RTT ELF's RAM budget and check flash/RAM placement."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def inspect(elf):
    tool_dir = Path(os.environ.get('RTT_EXEC_PATH', ROOT / '.toolchains' /
        'riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14/bin'))
    prefix = os.environ.get('RTT_CC_PREFIX', 'riscv64-unknown-elf-')
    sections = {}
    output = subprocess.check_output([tool_dir / (prefix + 'objdump'), '-h', elf], text=True)
    for name, size, vma, lma in re.findall(
            r'^\s*\d+\s+(\.\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', output, re.M):
        sections[name] = dict(bytes=int(size, 16), vma=int(vma, 16), lma=int(lma, 16))
    allocated = {name for name, flags in re.findall(
        r'^\s*\d+\s+(\.\S+)[^\n]*\n\s*([^\n]*)', output, re.M) if 'ALLOC' in flags}
    output = subprocess.check_output([tool_dir / (prefix + 'nm'), '-n', elf], text=True)
    symbols = {name: int(addr, 16) for addr, name in re.findall(
        r'^([0-9a-f]+)\s+\S\s+(\S+)$', output, re.M)}
    checks = {}
    nimble = 'ble_hs_init' in symbols
    usb = 'ab32_usb_poll' in symbols
    tf = 'drv_sdcard_init' in symbols
    ir = 'irrx_isr' in symbols
    keys = 'board_keys_init' in symbols
    gpio_irq = 'ab32_pin_irq_handler' in symbols
    rtc = 'rtc_transfer' in symbols
    led = 'board_led_poll' in symbols
    power = 'ab32_power_init' in symbols
    audec = 'ab32_audec_init' in symbols
    flash_functions = ['finsh_thread_entry', '_audio_dev_init', 'pcmcheck',
                       'rt_hw_sound_init', 'sound_configure', 'audio_thread_entry',
                       'strtol', 'strtoul', 'strcmp', 'strchr', 'strlen']
    if '__udivdi3' in symbols:
        flash_functions += ['__udivdi3']
    if audec:
        flash_functions += ['ab32_audec_init']
        assert 0x13800 <= symbols['dma'] <= 0x2f000 - 2312
        assert symbols['dma'] % 16 == 0

    if 'fmrx' in symbols:
        flash_functions += ['fmrx']
    if 'btctl' in symbols:
        flash_functions += ['btctl']
        if 'diagnose' in symbols:
            flash_functions += ['diagnose']
    if power:
        flash_functions += ['ab32_power_init', 'power_control', 'pmctl', 'power_snapshot',
                            'power_off_locked', 'power_capture_boot', 'power_sleep_once',
                            'ab32_irtc_align_second_locked',
                            'ab32_irtc_transaction', 'ab32_irtc_read8_locked',
                            'ab32_irtc_update8_locked', 'ab32_irtc_shutdown_update_locked']
    if nimble:
        flash_functions += ['ble_hs_init', 'ble_gatts_init', 'ble_gap_adv_start']
    if usb:
        flash_functions += ['ab32_usb_poll', 'usbd_initialize', 'usb_worker']
    if tf:
        flash_functions += ['drv_sdcard_init', 'tf_read', 'f_read', 'dfs_mount', 'strstr']
        assert 0x13800 <= symbols['staging'] <= 0x2f000 - 512
        assert symbols['staging'] % 32 == 0
    if ir:
        flash_functions += ['irrx', 'irrx_read', 'irrx_control']
    if keys:
        flash_functions += ['board_keys_init', 'board_keys_poll', 'keys', 'keys_read', 'keys_control']
    if led:
        flash_functions += ['main', 'board_led_init', 'board_led_poll', 'drv_pwm_control',
                            'rt_pwm_set', 'rt_pwm_enable', 'rt_pwm_disable', 'hal_pwm_mspinit']
    if rtc:
        flash_functions += ['ab32_rtc_init', 'ab32_rtc_get_secs', 'ab32_rtc_set_secs',
                            'rt_hw_rtc_register', 'rtcctl', 'time', 'gmtime_r', 'localtime_r', 'mktime']
    if gpio_irq:
        flash_functions += ['ab32_pin_irq_thread', 'ab32_pin_irq_enable',
                            'ab32_pin_attach_irq', 'ab32_pin_dettach_irq']
        if 'gpio_dispatch' in symbols:  # May inline into the worker.
            flash_functions += ['gpio_dispatch']
    for name in flash_functions:
        addr = symbols[name]
        assert 0x10000200 <= addr < 0x10100000, f'{name} is not in flash'
        checks[name] = hex(addr)
    ram_functions = ['rt_schedule', 'rt_sem_release', 'rt_hw_context_switch',
                 'os_cache_init', 'load_cache', '__wrap_bthw_isr_do',
                 'bb_run_loop', 'hci_host_recv_packet', 'audio_isr', 'uart_isr',
                 'hct_send_command', 'hct_transport_table', 'rt_vsnprintf', 'rt_smem_alloc', 'rt_device_open', 'rt_device_close']
    if audec:
        for prefix in ('configure_clock', 'stop_engine'):
            names = [n for n in symbols if n == prefix or n.startswith(prefix + '.')]
            assert names, prefix + ' missing'
            ram_functions += names
    ram_functions += ['ab32_pin_mode', 'hal_gpio_init', 'gpio_afinit', 'port_sfr', 'port_table']
    if power:
        ram_functions += ['ab32_irtc_sleep_locked', 'power_bridge_policy', 'power_cutoff',
                          'ab32_irtc_cutoff_locked', 'rt_tick_set', 'rt_tick_get']
    if rtc:
        ram_functions += ['rtc_transfer', 'rtc_byte', 'rtc_wait_idle', 'rtc_counter_read',
                          'rt_hw_interrupt_disable', 'rt_hw_interrupt_enable']
        # GCC may specialize the boolean argument when PM is disabled.
        rtc_bridge_symbols = [name for name in symbols if name == 'rtc_bridge_init' or
                              name.startswith('rtc_bridge_init.')]
        assert rtc_bridge_symbols, 'RTC bridge helper missing'
        ram_functions += rtc_bridge_symbols
    if gpio_irq:
        ram_functions += ['ab32_pin_irq_handler', 'get_wakeup_circuit', 'gpio_sample',
                          'gpio_configure_irqs', 'gpio_take_pin', 'gpio_change_pin',
                          'rt_hw_irq_enable', 'rt_hw_irq_disable', 'rt_hw_interrupt_install',
                          'rt_hw_interrupt_disable', 'rt_hw_interrupt_enable',
                          'rt_interrupt_enter', 'rt_interrupt_leave', 'pin_irq_table']
    if nimble:
        ram_functions += ['ble_npl_hw_enter_critical', 'ble_npl_hw_exit_critical',
                          'ble_npl_eventq_put', 'ble_mqueue_get', 'ble_mqueue_put',
                          'os_memblock_get', 'os_memblock_put', 'alloc_acl', 'free_acl']
    if ir:
        ram_functions += ['irrx_isr', 'irrx_push', 'irrx_release', 'irrx_switch',
                          'irrx_pop', 'irrx_snapshot', 'irrx_pin_regs', 'ab32_irrx_poll',
                          'rt_hw_irq_disable', 'rt_hw_interrupt_install',
                          'rt_hw_interrupt_disable', 'rt_hw_interrupt_enable',
                          'rt_interrupt_enter', 'rt_interrupt_leave', 'rt_tick_get',
                          'memcpy', 'memset']
    if ir or rtc:
        ram_functions += sorted(name for name in symbols
            if name.startswith(('__riscv_save_', '__riscv_restore_')))
    for name in ram_functions:
        addr = symbols[name]
        assert 0x13800 <= addr < 0x2f000 or 0x50000 <= addr < 0x54000 or 0x57000 <= addr < 0x57a00, f'{name} left SRAM'
        checks[name] = hex(addr)
    assert symbols['bb_get_without_vehicle'] == 0x11000
    assert sections['.bram']['bytes'] <= 10 * 1024
    assert sections['.heap']['bytes'] >= ((23 if tf else 27 if usb else 28 if nimble else 39) - 4 - ir - 2 * gpio_irq) * 1024
    assert sections['.ram1']['bytes'] <= 0x4000
    assert sections['.ram2']['vma'] == 0x57000
    assert sections['.ram2']['bytes'] <= 0xa00
    for section, prefix in (('.ram1', '__ram1'), ('.ram2', '__ram2'), ('.comm', '__comm')):
        for field in ('vma', 'lma'):
            assert symbols[prefix + '_' + field] == sections[section][field]
        assert symbols[prefix + '_size'] == sections[section]['bytes']
    assert sections['.audec_work']['vma'] == 0x54000
    assert sections['.audec_work']['bytes'] == 0x3000
    assert {'.comm', '.ram1', '.ram2', '.audec_work', '.bss', '.heap', '.bram'} <= allocated
    # Check whole allocated ranges, not just a few function entry points.
    for name, section in sections.items():
        start, size = section['vma'], section['bytes']
        if name in allocated and name != '.audec_work' and size:
            assert start >= 0x57000 or start + size <= 0x54000, f'{name} overlaps AUDEC'

    assert symbols['__bank_size'] == sections['.flash']['bytes']
    assert sections['.flash']['vma'] % 512 == 0
    assert sections['.comm']['vma'] % 512 == 0
    return dict(sections=sections, placement_checks=checks, nimble_enabled=nimble, usb_enabled=usb, tf_enabled=tf, ir_enabled=ir,
                keys_enabled=keys, gpio_irq_enabled=gpio_irq,
                rtc_enabled=rtc, led_breath_enabled=led, power_enabled=power, audec_enabled=audec,
                heap_region_bytes=sections['.heap']['bytes'],
                note='Heap region includes allocator metadata; use board free for runtime availability.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path, default=ROOT / 'rtt/bsp/rtthread.elf')
    args = parser.parse_args()
    print(json.dumps(inspect(args.elf), indent=2))
