#!/usr/bin/env python3
"""Test SPI1 G6 on an EMPTY PE2/PE3/PE4 header; uses the unified firmware shell.

No slave response or waveform timing is validated. Also tests one RTC sleep.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(r'\x1b(?:\[[0-9;]*[A-Za-z]|[78])')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    commands = ['list device', 'free', 'spi1check 200000', 'spi1check 1000000',
                'spi1check 5000000', 'pmctl sleep', 'spi1check 200000',
                'spi1check 0', 'free', 'keys status', 'pmctl status']
    result = subprocess.run([sys.executable, str(ROOT / 'tools/rtt/console.py'),
                             '--port', args.port, '--wait', '3', *commands],
                            capture_output=True, timeout=60)
    output = ANSI.sub('', result.stdout.decode('utf-8', errors='replace'))
    report = dict(port=args.port, commands=commands, output=output,
                  stderr=result.stderr.decode('utf-8', errors='replace'),
                  passed=False, external_slave_verified=False, waveform_verified=False)
    firmware = ROOT / 'rtt/bsp/rtthread.dcf'
    if firmware.exists():
        report['local_dcf_sha256'] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    try:
        assert result.returncode == 0, report['stderr']
        assert output.count('SPI1 controller self-test: PASS (0)') == 4, output
        assert output.count('RX pull-down completed=16 data=' + '00' * 16) == 4, output
        assert output.count('RX pull-up completed=16 data=' + 'ff' * 16) == 4, output
        timings = re.findall(r'TX benchmark bytes=4096 elapsed_ticks=(\d+) tick_hz=(\d+)', output)
        assert len(timings) == 4 and all(hz == '1000' for _, hz in timings), timings
        report['elapsed_ms'] = [int(t) for t, _ in timings]
        low, medium, high, low_after = report['elapsed_ms']
        assert low > medium > high > 0 and low_after > medium, timings
        assert 'sleep returned: 0 ' in output, output
        assert 'spi1check 0' in output and 'command failed -22' in output, output
        for label in ('total', 'used', 'available'):
            values = re.findall(r'(?m)^' + label + r'\s*:\s*(\d+)', output)
            assert len(values) == 2 and values[0] == values[1], (label, values)
            report['heap_' + label] = int(values[0])
        assert 'read_errors=0' in output and 'blockers=00 ' in output, output
        report['passed'] = True
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(output)
        print('Report:', args.output)


if __name__ == '__main__':
    main()
