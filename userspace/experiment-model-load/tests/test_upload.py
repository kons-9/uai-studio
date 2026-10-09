import pathlib
import sys
import tempfile
from unittest import mock
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tool"))
import manifest
import upload
from layout import BLOB_ADDRESS, WEIGHTS_ADDRESS


class FakePort:
    def __init__(self, reject=None):
        self.commands = []
        self.reject = reject

    def write(self, command, timeout):
        self.commands.append(command.decode().strip())

    def readline(self, timeout):
        if self.reject and self.reject in self.commands[-1]:
            return "> ERR invalid-argument"
        if self.commands[-1] == "model stat":
            return "MODEL state=verified weights=1030 blob=11 npu=unavailable"
        return "> MODEL OK abort" if self.commands[-1] == "model abort" else "> MODEL OK"


class UploadTests(unittest.TestCase):
    def test_chunks_and_abort(self):
        weights, blob = bytes(index & 0xff for index in range(1030)), bytes(range(11))
        value = manifest.Manifest(1, 1, 4, 4, WEIGHTS_ADDRESS, len(weights), zlib.crc32(weights),
                                  BLOB_ADDRESS, len(blob), zlib.crc32(blob))
        port = FakePort()
        upload.upload(port, value.encode(), weights, blob, 1)
        self.assertEqual(port.commands[-2:], ["model commit", "model stat"])
        self.assertEqual([command.split()[3] for command in port.commands if command.startswith("model chunk weights")], ["0", "512", "1024"])
        failed = FakePort("model chunk blob")
        with self.assertRaises(ValueError):
            upload.upload(failed, value.encode(), weights, blob, 1)
        self.assertEqual(failed.commands[-1], "model abort")
        with self.assertRaises(ValueError):
            upload.upload(FakePort(), value.encode(), bytes(len(weights)), blob, 1)

    def test_execution_and_crc(self):
        class RunPort(FakePort):
            def __init__(self, result):
                super().__init__()
                self.lines = iter(["MODEL OK running", result])

            def readline(self, timeout):
                if self.commands[-1] == "model abort":
                    return "MODEL OK abort"
                return next(self.lines)

        result = "MODEL RESULT npu=done output_crc=12345678 elapsed_ms=10 input=zeros"
        port = RunPort(result)
        self.assertEqual(upload.run_model(port, 1, 0x12345678), 0x12345678)
        self.assertEqual(port.commands, ["model run"])
        for line, expected in ((result, 0), (result.replace("done", "faulted"), None),
                               (result.replace("12345678", "invalid"), None)):
            port = RunPort(line)
            with self.subTest(line=line), self.assertRaises(ValueError):
                upload.run_model(port, 1, expected)
            self.assertEqual(port.commands[-1], "model abort")

    def test_invalid_payload_does_not_open_uart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            value = manifest.Manifest(1, 1, 4, 4, WEIGHTS_ADDRESS, 2, zlib.crc32(b"AB"),
                                      BLOB_ADDRESS, 2, zlib.crc32(b"CD"))
            (root / "manifest.bin").write_bytes(value.encode())
            (root / "weights.bin").write_bytes(b"XX")
            (root / "blob.bin").write_bytes(b"CD")
            arguments = ["upload.py", "--uart", "/no/device", "--manifest", str(root / "manifest.bin"),
                         "--weights", str(root / "weights.bin"), "--blob", str(root / "blob.bin")]
            with mock.patch.object(sys, "argv", arguments), mock.patch.object(upload, "Uart") as uart:
                with self.assertRaises(SystemExit) as failed:
                    upload.main()
                self.assertEqual(failed.exception.code, 1)
                uart.assert_not_called()


if __name__ == "__main__":
    unittest.main()
