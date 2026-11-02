#!/usr/bin/env python3
"""Check electrical-echo FinSH over the programming UART; close minicom first."""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from bluetrum_serial import SerialPort


def capture(port, seconds):
    result = bytearray()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            result.extend(port.read_some(timeout=min(.05, max(.001, deadline - time.monotonic()))))
        except TimeoutError:
            pass
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = dict(port=args.port, tests=[], passed=False)
    with SerialPort(args.port, 115200, restore_settings=False) as port:
        capture(port, .3)
        port.write_all(b'\r')
        assert b'msh ' in capture(port, .5), 'shell prompt missing'
        try:
            # No terminal local echo: every received printable byte comes from the wire.
            chars = []
            for value in b'version':
                port.write_all(bytes([value]))
                got = capture(port, .12)
                assert got.replace(b'\x1b7', b'') == bytes([value]), repr(got)
                chars.append(dict(sent=chr(value), received_hex=got.hex()))
            port.write_all(b'\r')
            response = capture(port, .7)
            assert response.count(b'5.2.2') == 1 and b'msh ' in response, repr(response)
            report['tests'].append(dict(name='each printable byte echoed once; version executes once',
                                        chars=chars, response=response.decode('ascii')))
            for name, command in [
                ('DEL backspace', b'versionx\x7f\r'),
                ('BS backspace', b'versionx\x08\r'),
                ('middle insertion', b'versin\x1b[Do\r'),
                ('left and right', b'version\x1b[D\x1b[C\r'),
                ('history up', b'\x1b[A\r'),
                ('tab completion', b'ver\t\r'),
            ]:
                response = bytearray()
                for value in command:
                    port.write_all(bytes([value]))
                    response.extend(capture(port, .04))
                response.extend(capture(port, .7))
                assert response.count(b'5.2.2') == 1 and b'msh ' in response, (name, response)
                report['tests'].append(dict(name=name, sent_hex=command.hex(),
                                            response=response.decode('ascii')))
            report['passed'] = True
        finally:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: one echo per letter; version, BS/DEL, insertion, arrows, history and tab on board')


if __name__ == '__main__':
    main()
