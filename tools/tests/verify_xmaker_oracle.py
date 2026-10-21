#!/usr/bin/env python3
"""Compare DCF bytes against unmodified Windows xmaker instructions.

Optional dependency: python3 -m pip install --target /tmp/xmaker-deps unicorn
Run: PYTHONPATH=/tmp/xmaker-deps python3 tools/tests/verify_xmaker_oracle.py
Use --write-evidence to refresh the checked-in hashes after all comparisons pass.
"""
import argparse
import hashlib
import json
from pathlib import Path

from xmaker_oracle import VendorPacker
import xmaker_re as x


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-evidence', action='store_true')
    parser.add_argument('--large', action='store_true', help='test the maximum supported app separately')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    exe = (root / 'tools/windows/riscv32-elf-xmaker.exe').read_bytes()
    header = (root / 'tools/header.bin').read_bytes()
    assert x.make_xor32_table() == x.extract_xor32_table(exe)
    assert x.make_crc16_table() == x.extract_crc16_table(exe)
    rows = []
    sizes = (1, 2, 511, 512, 513, 1023, 1024, 2048, 4095, 4096, 4097,
             65535, 65536, 65537, 126975, 126976, 126977, 131072, 262145, 1048576)
    if args.large:
        sizes = (x.MAX_APP_SIZE,)
    for size in sizes:
        app = bytes(((i * 73 + (i >> 8) * 19 + 29) & 255) for i in range(size))
        oracle = VendorPacker(exe)
        expected, body = oracle.pack(header, app)
        actual = x.build_dcf(header, app)
        if expected != actual:
            first = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b),
                         min(len(expected), len(actual)))
            raise AssertionError(f'size={size}: first difference at {first:#x}; '
                                 f'vendor size={len(expected)}, Python size={len(actual)}')
        rows.append({'app_size': size, 'app_sha256': sha(app), 'dcf_size': len(actual),
                     'dcf_sha256': sha(actual), 'equal': True})
        print(f'PASS app={size} dcf={len(actual)}', flush=True)
    result = {
        'method': 'Unicorn x86 execution of original PE instructions; not a Windows process run',
        'vendor_exe_sha256': sha(exe), 'header_sha256': sha(header),
        'entry_va': '0x40ea66', 'exit_va': '0x4104ac',
        'settings_source': 'zero defaults from constructor 0x4082f8..0x408327',
        'input_buffers': 'file bytes in zero-initialized 64 KiB chunks; app logical size rounded to 512',
        'hooks': {'0x411cf4': 'operator new', '0x411d2e': 'operator new[]',
                  '0x421880': 'memset', '0x41d651': 'printf (discard output)',
                  '0x41ff15': 'throw (raises; never called in passing cases)'},
        'limitations': ['XM parser and Windows file I/O not executed',
                        'default script settings only; no XRES/XCFG/KEY options',
                        'no serial or hardware verification'],
        'tables_equal': {'crc16_entries': 256, 'xor32_entries': 256},
        'cases': rows,
    }
    if args.write_evidence:
        path = Path(__file__).with_name('xmaker_oracle_limit_evidence.json' if args.large
                                      else 'xmaker_oracle_evidence.json')
        path.write_text(json.dumps(result, indent=2) + '\n')
        print(f'Wrote {path}')
    print(f'{len(rows)} complete DCF byte comparisons passed')


if __name__ == '__main__':
    main()
