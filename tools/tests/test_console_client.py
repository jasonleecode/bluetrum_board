"""Host framing checks: distinguish the hardware echo from an actual reply."""
import os
from pathlib import Path
import pty
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux"))
from bluetrum_serial import SerialPort
from console import Console


class ClientTests(unittest.TestCase):
    def test_echo_and_fragmented_response_on_pty(self):
        master, slave = pty.openpty()
        errors = []

        def board():
            try:
                data = bytearray()
                while not data.endswith(b'\r'):
                    char = os.read(master, 1)
                    data.extend(char)
                    os.write(master, char)
                self.assertEqual(data, b'ping\r')
                for part in (b'O', b'K po', b'ng\r', b'\n'):
                    os.write(master, part)
            except BaseException as error:
                errors.append(error)

        try:
            with SerialPort(os.ttyname(slave)) as port:
                worker = threading.Thread(target=board, daemon=True)
                worker.start()
                self.assertEqual(Console(port).command('ping'), 'OK pong')
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive())
                self.assertFalse(errors, errors)
        finally:
            os.close(master)
            os.close(slave)

    def test_invalid_input_does_not_open_or_write_port(self):
        client = Console(None)
        for command in ('', 'x' * 96, 'ping\nreboot', '中文'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                client.command(command)

    def test_hardware_echo_is_not_treated_as_a_reply(self):
        class EchoOnly:
            def flush_input(self): pass
            def write_all(self, data, timeout): self.echo = data
            def read_exact(self, size, timeout):
                if not self.echo: raise TimeoutError('no target response')
                data, self.echo = self.echo, b''
                return data
        with self.assertRaises(TimeoutError):
            Console(EchoOnly(), timeout=.1).command('ping')


if __name__ == "__main__":
    unittest.main()
