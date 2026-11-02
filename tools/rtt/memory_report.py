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
    output = subprocess.check_output([tool_dir / (prefix + 'nm'), '-n', elf], text=True)
    symbols = {name: int(addr, 16) for addr, name in re.findall(
        r'^([0-9a-f]+)\s+\S\s+(\S+)$', output, re.M)}
    checks = {}
    for name in ('finsh_thread_entry', '_audio_dev_init', 'pcmcheck'):
        addr = symbols[name]
        assert 0x10000200 <= addr < 0x10100000, f'{name} is not in flash'
        checks[name] = hex(addr)
    for name in ('rt_schedule', 'rt_sem_release', 'rt_hw_context_switch',
                 'os_cache_init', 'load_cache', '__wrap_bthw_isr_do',
                 'bb_run_loop', 'hci_host_recv_packet', 'audio_isr', 'uart_isr'):
        addr = symbols[name]
        assert 0x13800 <= addr < 0x2f000 or 0x50000 <= addr < 0x57a00, f'{name} left SRAM'
        checks[name] = hex(addr)
    assert symbols['bb_get_without_vehicle'] == 0x11000
    assert sections['.bram']['bytes'] <= 10 * 1024
    assert sections['.heap']['bytes'] >= 40 * 1024
    assert sections['.ram1']['bytes'] <= 0x7a00
    assert symbols['__bank_size'] == sections['.flash']['bytes']
    assert sections['.flash']['vma'] % 512 == 0
    assert sections['.comm']['vma'] % 512 == 0
    return dict(sections=sections, placement_checks=checks,
                heap_region_bytes=sections['.heap']['bytes'],
                note='Heap region includes allocator metadata; use board free for runtime availability.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path, default=ROOT / 'rtt/bsp/rtthread.elf')
    args = parser.parse_args()
    print(json.dumps(inspect(args.elf), indent=2))
