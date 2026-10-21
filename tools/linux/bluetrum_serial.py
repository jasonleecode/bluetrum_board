"""Linux serial byte transport, independent of chip commands and flashing policy."""

import errno
import os
import select
import termios
import time
import tty


class SerialPort:
    """Explicit 8N1 serial port, with bounded reads/writes and no port scanning."""

    def __init__(self, path: str, baud: int = 115200, timeout: float = 2.0):
        if timeout <= 0:
            raise ValueError("serial timeout must be positive")
        self.timeout = timeout
        self.fd = None
        self.original_settings = None
        self._baud_value(baud)
        descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.fd = descriptor
        try:
            self.original_settings = termios.tcgetattr(descriptor)
            tty.setraw(descriptor, termios.TCSANOW)
            settings = termios.tcgetattr(descriptor)
            settings[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE | getattr(termios, "CRTSCTS", 0))
            settings[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
            # In nonblocking mode VMIN=1 makes a zero-byte read signal a hangup.
            settings[6][termios.VMIN] = 1
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(descriptor, termios.TCSANOW, settings)
            self.set_baud(baud)
        except BaseException:
            self.close()
            raise

    @staticmethod
    def _baud_value(baud: int):
        value = getattr(termios, f"B{baud}", None)
        if value is None or baud <= 0:
            raise ValueError(f"unsupported serial baud rate: {baud}")
        return value

    def _descriptor(self):
        if self.fd is None:
            raise ValueError("serial port is closed")
        return self.fd

    def set_baud(self, baud: int) -> None:
        value = self._baud_value(baud)
        fd = self._descriptor()
        settings = termios.tcgetattr(fd)
        settings[4] = settings[5] = value
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        self.baud = baud

    def flush_input(self) -> None:
        termios.tcflush(self._descriptor(), termios.TCIFLUSH)

    def _deadline(self, timeout):
        duration = self.timeout if timeout is None else timeout
        if duration <= 0:
            raise ValueError("serial timeout must be positive")
        return time.monotonic() + duration

    def _wait(self, deadline: float, writing: bool) -> int:
        fd = self._descriptor()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("serial write timed out" if writing else "serial read timed out")
            try:
                readable, writable, _ = select.select([] if writing else [fd], [fd] if writing else [], [], remaining)
            except InterruptedError:
                continue
            if readable or writable:
                return fd

    def _read(self, maximum: int, deadline: float) -> bytes:
        while True:
            fd = self._wait(deadline, False)
            try:
                data = os.read(fd, maximum)
            except OSError as error:
                if error.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                    continue
                raise
            if not data:
                raise EOFError("serial device disconnected")
            return data

    def read_some(self, maximum: int = 4096, timeout=None) -> bytes:
        if maximum <= 0:
            raise ValueError("read size must be positive")
        return self._read(maximum, self._deadline(timeout))

    def read_exact(self, length: int, timeout=None) -> bytes:
        if length < 0:
            raise ValueError("read size must not be negative")
        deadline = self._deadline(timeout)
        result = bytearray()
        while len(result) < length:
            result.extend(self._read(length - len(result), deadline))
        return bytes(result)

    def write_all(self, data: bytes, timeout=None) -> None:
        deadline = self._deadline(timeout)
        remaining = memoryview(data)
        while remaining:
            fd = self._wait(deadline, True)
            try:
                count = os.write(fd, remaining)
            except OSError as error:
                if error.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                    continue
                raise
            if count == 0:
                raise EOFError("serial device disconnected during write")
            remaining = remaining[count:]

    def close(self) -> None:
        if self.fd is not None:
            descriptor, self.fd = self.fd, None
            try:
                if self.original_settings is not None:
                    termios.tcsetattr(descriptor, termios.TCSANOW, self.original_settings)
            except (OSError, termios.error):
                pass  # The USB device may have disappeared; still release the FD.
            finally:
                os.close(descriptor)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
