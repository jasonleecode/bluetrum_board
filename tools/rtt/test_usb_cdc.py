#!/usr/bin/env python3
"""Linux native CDC echo verification; requires a real AB32 USB data connection."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import select
import termios
import time


def identity(port):
    name = Path(os.path.realpath(port)).name
    node = (Path('/sys/class/tty') / name / 'device').resolve()
    for p in (node, *node.parents):
        if (p / 'idVendor').is_file():
            vid, pid = (p / 'idVendor').read_text().strip(), (p / 'idProduct').read_text().strip()
            if (vid, pid) != ('ffff', 'ab32'):
                raise RuntimeError(f'{port}: expected bench AB32 CDC ffff:ab32, found {vid}:{pid}')
            return dict(vid=vid, pid=pid, product=(p / 'product').read_text().strip())
    raise RuntimeError(f'{port}: not a USB CDC tty; do not use the CH340 programming port')


def exchange(fd, payload, timeout=10):
    sent = 0
    received = bytearray()
    end = time.monotonic() + timeout
    while sent < len(payload) or len(received) < len(payload):
        left = end - time.monotonic()
        if left <= 0:
            raise TimeoutError(f'CDC echo: sent {sent}/{len(payload)}, received {len(received)}')
        read, write, _ = select.select([fd], [fd] if sent < len(payload) else [], [], left)
        if write:
            try:
                sent += os.write(fd, payload[sent:])
            except BlockingIOError:
                pass
        if read:
            try:
                data = os.read(fd, max(1, len(payload) - len(received)))
            except BlockingIOError:
                continue
            if not data:
                raise ConnectionError('CDC disconnected during transfer')
            received.extend(data)
    if received != payload:
        raise AssertionError('CDC echo data mismatch')
    return hashlib.sha256(received).hexdigest()


def run(args):
    device = identity(args.port)
    results = []
    for cycle in range(args.rounds):
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        saved = termios.tcgetattr(fd)
        try:
            cfg = termios.tcgetattr(fd)
            cfg[0] = cfg[1] = cfg[3] = 0
            cfg[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            cfg[4] = cfg[5] = termios.B115200
            cfg[6][termios.VMIN] = cfg[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, cfg)
            termios.tcflush(fd, termios.TCIOFLUSH)
            cases = []
            for n in (1, 7, 63, 64, 65, 127, 128, 129, 1024, 4097):
                payload = bytes((i * 37 + cycle * 13) & 255 for i in range(n))
                cases.append(dict(bytes=n, sha256=exchange(fd, payload)))
            results.append(dict(cycle=cycle + 1, cases=cases))
        finally:
            try:
                termios.tcsetattr(fd, termios.TCSANOW, saved)
            finally:
                os.close(fd)
        print(f'PASS CDC cycle {cycle + 1}: binary echo 1..4097 bytes', flush=True)
    report = dict(recorded_at=datetime.now(timezone.utc).isoformat(), port=args.port,
                  device=device, hardware_verified=True, cycles=results,
                  note='Reopens the tty; physical unplug/replug and USB bus reset require separate tests.')
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='native CDC tty, e.g. /dev/ttyACM0')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 100:
        parser.error('--rounds must be 1..100')
    run(args)
