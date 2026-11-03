#!/usr/bin/env python3
"""Test date first-setting/readback; finish with the host's current UTC+8 time."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import time
from test_rtc_uart import SerialPort, capture, ANSI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-unset', action='store_true',
                        help='Require an already-unset RTC; never force the hardware invalid')
    args = parser.parse_args()
    report = {'port': args.port, 'commands': [], 'passed': False}
    firmware = Path(__file__).resolve().parents[2] / 'rtt/bsp/rtthread.dcf'
    report['firmware_sha256'] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    try:
        with SerialPort(args.port, 115200, restore_settings=False) as port:
            capture(port, .2)
            port.write_all(b'\r')
            capture(port, .2)

            def command(cmd):
                for byte in (cmd + '\r').encode('ascii'):
                    port.write_all(bytes([byte]))
                    time.sleep(.005)
                text = ANSI.sub(b'', capture(port, .4)).decode(errors='replace')
                report['commands'].append({'command': cmd, 'output': text})
                assert 'msh ' in text, text
                return text

            def seconds():
                text = command('rtcctl get')
                match = re.search(r'RTC UTC seconds=(\d+)', text)
                assert match, text
                return int(match[1])

            def sync_host():
                now = datetime.now(timezone(timedelta(hours=8)))
                text = command(now.strftime('date %Y %m %d %H %M %S'))
                assert 'now:' in text and 'failed' not in text, text
                assert abs(seconds() - time.time()) < 3

            initial = command('rtcctl status')
            unset = 'valid=0' in initial
            report['initially_unset'] = unset
            if args.require_unset:
                assert unset, 'RTC already valid; first-setting case cannot be verified'
            before = command('date')
            if unset:
                assert 'Read RTC failed' in before and '1970' not in before, before
            try:
                text = command('date 2026 11 03 12 34 56')
                assert 'now:' in text and 'failed' not in text, text
                if unset:
                    assert 'old: unavailable' in text, text
                assert 1793680496 <= seconds() <= 1793680498
                assert 'valid=1' in command('rtcctl status')
                text = command('date')
                assert '2026' in text and 'timezone: UTC+08:00:00' in text, text
                first = seconds()
                time.sleep(2)
                second = seconds()
                assert 1 <= second - first <= 4, (first, second)
                report['running_seconds_delta'] = second - first
            finally:
                sync_host()
            report['final_date'] = command('date')
            report['final_rtc_status'] = command('rtcctl status')
            report['free'] = command('free')
            report['passed'] = True
    finally:
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('PASS: date setting/readback, running RTC, host time restored; initial unset:', unset)


if __name__ == '__main__':
    main()
