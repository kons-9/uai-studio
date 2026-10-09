import fcntl
import os
import select
import termios
import time
import tty


class Uart:
    def __init__(self, device, baud=115200):
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise ValueError("unsupported baud rate")
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.original = None
        self.buffer = bytearray()
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.original = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[2] |= termios.CLOCAL | termios.CREAD
            settings[2] &= ~getattr(termios, "CRTSCTS", 0)
            settings[4] = settings[5] = speed
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.fd is not None:
            try:
                if self.original is not None:
                    termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
                fcntl.ioctl(self.fd, termios.TIOCNXCL)
            except OSError:
                pass
            finally:
                os.close(self.fd)
                self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *arguments):
        self.close()

    def write(self, data, timeout):
        deadline = time.monotonic() + timeout
        pending = memoryview(data)
        while pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError("UART write timed out")
            try:
                written = os.write(self.fd, pending)
            except BlockingIOError:
                continue
            if written == 0:
                raise ConnectionError("UART disconnected")
            pending = pending[written:]

    def readline(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self.buffer[:newline]).rstrip(b"\r")
                del self.buffer[:newline + 1]
                return line.decode("ascii", errors="replace")
            if len(self.buffer) >= 4096:
                raise ValueError("UART line exceeds 4096 bytes")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError("UART result timed out")
            try:
                data = os.read(self.fd, 4096 - len(self.buffer))
            except BlockingIOError:
                continue
            if not data:
                raise ConnectionError("UART disconnected")
            self.buffer.extend(data)

    def flush_input(self):
        self.buffer.clear()
        termios.tcflush(self.fd, termios.TCIFLUSH)

    def read_until(self, marker, timeout):
        if not marker:
            raise ValueError("empty UART marker")
        deadline = time.monotonic() + timeout
        while True:
            end = self.buffer.find(marker)
            while end > 0 and self.buffer[end - 1] != 10:
                end = self.buffer.find(marker, end + 1)
            if end >= 0:
                end += len(marker)
                result = bytes(self.buffer[:end])
                del self.buffer[:end]
                return result.decode("ascii", errors="replace")
            if len(self.buffer) >= 4096:
                raise ValueError("UART response exceeds 4096 bytes")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError("UART prompt timed out")
            try:
                data = os.read(self.fd, 4096 - len(self.buffer))
            except BlockingIOError:
                continue
            if not data:
                raise ConnectionError("UART disconnected")
            self.buffer.extend(data)
