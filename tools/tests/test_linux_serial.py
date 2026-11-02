"""Use a pseudo-terminal to check byte transparency, fragmentation and timeouts."""

import os
from pathlib import Path
import pty
import select
import sys
import termios
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux"))
from bluetrum_serial import SerialPort


class SerialTransportTests(unittest.TestCase):
    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        self.port = SerialPort(os.ttyname(self.slave), timeout=0.2)

    def tearDown(self):
        self.port.close()
        os.close(self.master)
        os.close(self.slave)

    def test_raw_bytes_and_fragmented_reply(self):
        payload = bytes(range(256))
        self.port.write_all(payload)
        received = bytearray()
        while len(received) < len(payload):
            received.extend(os.read(self.master, len(payload) - len(received)))
        self.assertEqual(received, payload)

        def reply():
            os.write(self.master, payload[:17])
            time.sleep(0.01)
            os.write(self.master, payload[17:])

        worker = threading.Thread(target=reply)
        worker.start()
        try:
            self.assertEqual(self.port.read_exact(256), payload)
        finally:
            worker.join(timeout=1)
        self.assertFalse(worker.is_alive())

    def test_timeout_and_settings_restore(self):
        with self.assertRaises(TimeoutError):
            self.port.read_exact(1, timeout=0.02)
        self.port.set_baud(57600)
        self.assertEqual(termios.tcgetattr(self.slave)[4], termios.B57600)
        self.port.close()
        self.assertEqual(termios.tcgetattr(self.slave), self.original)
        with self.assertRaises(ValueError):
            self.port.write_all(b"x")

    def test_partial_reply_obeys_total_timeout(self):
        os.write(self.master, b"partial")
        with self.assertRaises(TimeoutError):
            self.port.read_exact(8, timeout=0.02)

    def test_single_wire_console_keeps_host_echo_disabled_after_close(self):
        self.port.close()
        settings = termios.tcgetattr(self.slave)
        settings[3] |= termios.ECHO | termios.ICANON
        termios.tcsetattr(self.slave, termios.TCSANOW, settings)
        self.port = SerialPort(os.ttyname(self.slave), restore_settings=False)
        self.port.close()
        self.assertFalse(termios.tcgetattr(self.slave)[3] & (termios.ECHO | termios.ICANON))
        os.write(self.master, b'heartbeat: LED ON\r\n')
        # Incoming application logs must not be reflected back to the wire.
        self.assertFalse(select.select([self.master], [], [], .02)[0])

    def test_invalid_parameters(self):
        with self.assertRaises(ValueError):
            self.port.set_baud(12345)
        with self.assertRaises(ValueError):
            self.port.read_exact(-1)
        with self.assertRaises(ValueError):
            SerialPort("/does/not/exist", timeout=0)


if __name__ == "__main__":
    unittest.main()
