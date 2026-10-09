import pathlib
import sys
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import manifest
import upload


class FakePort:
    def __init__(self, reject=None):
        self.commands = []
        self.reject = reject

    def write(self, command, timeout):
        self.commands.append(command.decode().strip())

    def readline(self, timeout):
        if self.reject and self.reject in self.commands[-1]:
            return "> ERR invalid-argument"
        return "> MODEL OK abort" if self.commands[-1] == "model abort" else "> MODEL OK"


class UploadTests(unittest.TestCase):
    def test_chunks_and_abort(self):
        weights, blob = bytes(range(65)), bytes(range(11))
        value = manifest.Manifest(1, 1, 4, 4, 0x91010000, len(weights), zlib.crc32(weights),
                                  0x91020000, len(blob), zlib.crc32(blob))
        port = FakePort()
        upload.upload(port, value.encode(), weights, blob, 1)
        self.assertEqual(port.commands[-1], "model commit")
        self.assertEqual([command.split()[3] for command in port.commands if command.startswith("model chunk weights")], ["0", "32", "64"])
        failed = FakePort("model chunk blob")
        with self.assertRaises(ValueError):
            upload.upload(failed, value.encode(), weights, blob, 1)
        self.assertEqual(failed.commands[-1], "model abort")
        with self.assertRaises(ValueError):
            upload.upload(FakePort(), value.encode(), bytes(len(weights)), blob, 1)


if __name__ == "__main__":
    unittest.main()