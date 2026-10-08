import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import console


class Port:
    def __init__(self, response):
        self.response = response
        self.sent = []

    def write(self, data, timeout):
        self.sent.append(data)

    def read_until(self, marker, timeout):
        if marker != b"> ":
            raise AssertionError(marker)
        return self.response


class ConsoleTests(unittest.TestCase):
    def test_command_response_and_error(self):
        port = Port("\r\ncam stat\r\ncam: ae=1\r\n> ")
        self.assertIn("cam: ae=1", console.execute(port, "cam stat", 2))
        self.assertEqual(port.sent, [b"cam stat\r"])
        port.response = "\r\nERR invalid-argument\r\n> "
        with self.assertRaises(ValueError):
            console.execute(port, "cam area", 2)
        with self.assertRaises(ValueError):
            console.execute(port, "x" * 128, 2)


if __name__ == "__main__":
    unittest.main()