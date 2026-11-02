#!/usr/bin/env python3
"""Talk to the bare LED/serial firmware through the programming USB port."""

import argparse
import sys
import time

from bluetrum_serial import SerialPort


class Console:
    def __init__(self, port, timeout=2.0):
        self.port = port
        self.timeout = timeout

    def command(self, text):
        data = text.encode("ascii")
        if not data or len(data) > 95 or any(byte < 32 or byte > 126 for byte in data):
            raise ValueError("command must contain 1..95 printable ASCII bytes")
        # CH340 TX and RX are joined on this board. Drain each hardware echo
        # before sending the next character, then wait for one complete reply.
        self.port.flush_input()
        for byte in data + b"\r":
            sent = bytes((byte,))
            self.port.write_all(sent, timeout=self.timeout)
            got = self.port.read_exact(1, timeout=self.timeout)
            if got != sent:
                raise RuntimeError(f"hardware echo mismatch: sent {sent.hex()}, got {got.hex()}")
        deadline = time.monotonic() + self.timeout
        reply = bytearray()
        while len(reply) < 256:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("board did not finish its reply")
            byte = self.port.read_exact(1, timeout=remaining)
            if byte == b"\n":
                result = reply.rstrip(b"\r").decode("ascii")
                if not result.startswith(("OK ", "ERR ")):
                    raise RuntimeError(f"invalid board reply: {result!r}")
                return result
            reply.extend(byte)
        raise RuntimeError("board reply exceeds 255 bytes")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="e.g. /dev/ttyUSB0")
    parser.add_argument("command", nargs="*", help="omit for interactive mode")
    args = parser.parse_args(argv)
    try:
        with SerialPort(args.port, 115200, restore_settings=False) as port:
            console = Console(port)
            if args.command:
                reply = console.command(" ".join(args.command))
                print(reply)
                return int(reply.startswith("ERR "))
            print("AB32 console: help for commands, quit to exit (115200 8N1).")
            while True:
                try:
                    command = input("ab32> ").strip()
                except EOFError:
                    break
                if command in ("quit", "exit"):
                    break
                if command:
                    print(console.command(command))
    except (OSError, ValueError, RuntimeError, EOFError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
