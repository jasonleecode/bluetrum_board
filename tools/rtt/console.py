#!/usr/bin/env python3
"""Run bounded RTT shell commands on the board's shared programming UART."""
import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'linux'))
from bluetrum_serial import SerialPort


def capture(port, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            data = port.read_some(timeout=min(0.1, max(0.001, end - time.monotonic())))
        except TimeoutError:
            continue
        sys.stdout.write(data.decode('utf-8', errors='replace'))
        sys.stdout.flush()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--wait', type=float, default=2)
    p.add_argument('commands', nargs='*')
    a = p.parse_args()
    if not 0 < a.wait <= 60:
        p.error('--wait must be in (0, 60]')
    for command in a.commands:
        if len(command) > 120 or any(ord(c) < 32 or ord(c) > 126 for c in command):
            p.error('invalid shell command')
    with SerialPort(a.port, 115200, restore_settings=False) as port:
        capture(port, 0.2)
        for command in a.commands:
            port.write_all(b'\r')
            capture(port, 0.15)
            # The shared programming UART echoes each byte electrically.
            # RTT input is polled; pace bytes to avoid overrunning its FIFO.
            for byte in (command + '\r').encode():
                port.write_all(bytes([byte]))
                time.sleep(0.005)
            capture(port, a.wait)
        if not a.commands:
            capture(port, a.wait)


if __name__ == '__main__':
    main()
