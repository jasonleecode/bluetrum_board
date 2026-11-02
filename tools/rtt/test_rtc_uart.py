#!/usr/bin/env python3
"""Validate native RTC read/write and short-term cadence; leaves host UTC time set."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from bluetrum_serial import SerialPort

ANSI = re.compile(rb'\x1b(?:\[[0-9;]*[A-Za-z]|[78])')


def capture(port, seconds):
    data = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            data.extend(port.read_some(timeout=min(.05, max(.001, end - time.monotonic()))))
        except TimeoutError:
            pass
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=30)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 120:
        parser.error('--seconds must be 10..120')
    report = dict(port=args.port, commands=[], samples=[], passed=False)
    with SerialPort(args.port, 115200, restore_settings=False) as port:
        capture(port, .3)
        port.write_all(b'\r')
        assert b'msh ' in capture(port, .5), 'no shell prompt'

        def command(text, allow_error=False):
            start = time.monotonic()
            for b in (text + '\r').encode('ascii'):
                port.write_all(bytes([b]))
                time.sleep(.01)
            raw = capture(port, .4)
            clean = ANSI.sub(b'', raw).decode('ascii', errors='replace')
            report['commands'].append(dict(command=text, text=clean,
                                            raw_hex=raw.hex(), elapsed=time.monotonic()-start))
            assert 'msh ' in clean, ('no prompt', clean)
            if not allow_error:
                assert 'RTC error' not in clean and 'command failed' not in clean, clean
            return clean

        def read_seconds():
            host_before = time.time()
            mono_before = time.monotonic()
            output = command('rtcctl get')
            match = re.search(r'RTC UTC seconds=(\d+)', output)
            assert match, output
            return dict(rtc=int(match[1]), host_before=host_before, host_after=time.time(),
                        mono_before=mono_before, mono_after=time.monotonic())

        try:
            command('version')
            command('list device')
            report['free_before'] = command('free')
            report['status_before'] = command('rtcctl status')
            report['initial_get'] = command('rtcctl get', allow_error=True)
            for value in (0, 0x12345678):
                command(f'rtcctl set {value}')
                got = read_seconds()['rtc']
                assert value <= got <= value + 2, (value, got)
            # Hardware has 32 unsigned bits; the toolchain may use signed
            # 32-bit time_t. Rejecting post-2038 values is correct in that ABI.
            before = read_seconds()['rtc']
            response = command('rtcctl set 2147483648', allow_error=True)
            if 'command failed -22' in response:
                report['time_t_max_seconds'] = 0x7fffffff
                after = read_seconds()['rtc']
                assert before <= after <= before + 3
            else:
                report['time_t_max_seconds'] = 0xffffffff
                assert 0x80000000 <= read_seconds()['rtc'] <= 0x80000002
            value = report['time_t_max_seconds'] - 15
            command(f'rtcctl set {value}')
            assert value <= read_seconds()['rtc'] <= value + 2
            for invalid in ('-1', '4294967296', '123x'):
                before = read_seconds()['rtc']
                response = command('rtcctl set ' + invalid, allow_error=True)
                assert 'command failed' in response, response
                after = read_seconds()['rtc']
                assert before <= after <= before + 3, (invalid, before, after)
            command(f'rtcctl set {int(time.time())}')
            first = read_seconds()
            report['samples'].append(first)
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                time.sleep(min(5, max(0, end - time.monotonic())))
                sample = read_seconds()
                report['samples'].append(sample)
                elapsed = sample['mono_before'] - first['mono_before']
                advance = sample['rtc'] - first['rtc']
                print(f'RTC +{advance}s / host +{elapsed:.2f}s', flush=True)
                assert abs(advance - elapsed) <= 2, 'RTC is stopped or cadence is wrong'
                # libc/device open/control path must not reset the counter.
                command('date')
            last = report['samples'][-1]
            report['rtc_elapsed'] = last['rtc'] - first['rtc']
            report['host_elapsed'] = last['mono_before'] - first['mono_before']
            assert report['rtc_elapsed'] > 0
            report['free_after'] = command('free')
            for label in ('total', 'used', 'available'):
                pattern = rf'(?m)^{label}\s*:\s*(\d+)'
                before = re.search(pattern, report['free_before'])
                after = re.search(pattern, report['free_after'])
                assert before and after and before[1] == after[1], 'heap usage changed'
            report['keys_after'] = command('keys status')
            assert 'read_errors=0' in report['keys_after']
            report['passed'] = True
        finally:
            try:
                report['restore_host_utc'] = int(time.time())
                command(f"rtcctl set {report['restore_host_utc']}")
                report['final_read'] = read_seconds()
                report['final_date'] = command('date')
                report['status_after'] = command('rtcctl status')
            except Exception as exc:
                report['restore_error'] = str(exc)
                report['passed'] = False
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + '\n')
    assert report['passed'], report.get('restore_error', 'RTC validation failed')
    print('PASS: RTC byte order/range, invalid input rejection, cadence and libc access; host UTC restored')


if __name__ == '__main__':
    main()
