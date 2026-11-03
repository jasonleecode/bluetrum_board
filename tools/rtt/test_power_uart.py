#!/usr/bin/env python3
"""Exercise RTC-woken sleep on a connected board; never executes power-off."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from bluetrum_serial import SerialPort

ANSI = re.compile(rb'\x1b(?:\[[0-9;]*[A-Za-z]|[78])')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cycles', type=int, default=10)
    args = p.parse_args()
    if not 1 <= args.cycles <= 30:
        p.error('cycles must be 1..30')
    report = dict(port=args.port, passed=False, commands=[], sleeps=[], poweroff_executed=False)
    try:
        with SerialPort(args.port, 115200, restore_settings=False) as port:
            def prompt(timeout=5):
                raw = bytearray()
                end = time.monotonic() + timeout
                while time.monotonic() < end:
                    try:
                        raw.extend(port.read_some(timeout=.1))
                    except TimeoutError:
                        continue
                    clean = ANSI.sub(b'', raw).decode('ascii', errors='replace')
                    if 'msh />' in clean:
                        return clean
                raise AssertionError('shell did not return: ' + repr(bytes(raw)))

            port.write_all(b'\r')
            report['initial_prompt'] = prompt()

            def command(text):
                for byte in text.encode('ascii'):
                    port.write_all(bytes([byte]))
                    time.sleep(.005)
                start = time.monotonic()
                port.write_all(b'\r')
                output = prompt()
                elapsed = time.monotonic() - start
                report['commands'].append(dict(command=text, output=output,
                    start_monotonic=start, elapsed=elapsed))
                assert 'command failed' not in output, output
                assert 'Thread Operating System' not in output or text == 'version', 'unexpected reboot'
                return output, elapsed

            command('version')
            status, _ = command('pmctl status')
            assert 'blockers=00' in status and 'rails=7' in status, status
            report['free_before'] = command('free')[0]
            rtc_before, _ = command('rtcctl status')
            host_before = time.monotonic()
            before = int(re.search(r'count=(\d+)', rtc_before)[1])
            for i in range(args.cycles):
                output, elapsed = command('pmctl sleep')
                match = re.search(r'sleep returned: (-?\d+) tick_delta=(\d+) entries=(\d+)', output)
                assert match and int(match[1]) == 0, output
                ticks, entries = int(match[2]), int(match[3])
                assert 900 <= ticks <= 2200, (ticks, output)
                assert abs(ticks / 1000 - elapsed) < .15, (ticks, elapsed)
                assert entries == 1, ('unexpected repeated/early wake', entries)
                report['sleeps'].append(dict(cycle=i+1, ticks=ticks, entries=entries, host_seconds=elapsed))
                print(f'{i+1}/{args.cycles}: sleep returned, {ticks} ticks / {elapsed:.3f}s, entries={entries}', flush=True)
                status, _ = command('pmctl status')
                assert 'blockers=00' in status and 'rails=7' in status, status
            rtc_after, _ = command('rtcctl status')
            host_elapsed = time.monotonic() - host_before
            advance = (int(re.search(r'count=(\d+)', rtc_after)[1]) - before) & 0xffffffff
            assert abs(advance - host_elapsed) < 2, (advance, host_elapsed)
            report['rtc_elapsed'] = advance
            report['host_elapsed'] = host_elapsed
            report['free_after'] = command('free')[0]
            for label in ('total', 'used', 'available'):
                pattern = rf'(?m)^{label}\s*:\s*(\d+)'
                assert re.search(pattern, report['free_before'])[1] == re.search(pattern, report['free_after'])[1]
            report['keys_after'] = command('keys status')[0]
            assert 'read_errors=0' in report['keys_after']
            report['led_after'] = command('led status')[0]
            assert 'error=0' in report['led_after']
            report['threads_after'] = command('ps')[0]
            report['passed'] = True
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print('PASS: repeated RTC wake/return, elapsed tick/RTC cadence, unchanged heap and responsive shell')


if __name__ == '__main__':
    main()
