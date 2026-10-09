import pathlib
import sys
import tempfile
from unittest import mock
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tool"))
import manifest
import upload
from layout import BLOB_ADDRESS, WEIGHTS_ADDRESS, UPLOAD_CHUNK_BYTES


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
        self.assertEqual([command.split()[3] for command in port.commands if command.startswith("model chunk weights")],
                 [str(offset) for offset in range(0, len(weights), UPLOAD_CHUNK_BYTES)])
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

    def test_input_and_result_transfer(self):
        payload = bytes(range(150))
        port = FakePort()
        upload.upload_input(port, payload, 1)
        self.assertTrue(port.commands[0].startswith("model input begin 150 "))
        self.assertEqual(port.commands[-1], "model input commit")
        direct = FakePort()
        upload.upload_input(direct, payload, 1, direct=True)
        self.assertEqual(len(direct.commands), 1)
        self.assertTrue(direct.commands[0].startswith("model input adopt 150 "))
        class ResultPort(FakePort):
            def readline(self, timeout):
                offset, size = map(int, self.commands[-1].split()[2:])
                return f"MODEL DATA {offset} {payload[offset:offset + size].hex()}"
        self.assertEqual(upload.read_result(ResultPort(), len(payload), zlib.crc32(payload), 1), payload)
        with self.assertRaises(ValueError):
            upload.read_result(ResultPort(), len(payload), 0, 1)

    def test_v2_header_transfer(self):
        value = manifest.Manifest(1201, 1, 3, 4, WEIGHTS_ADDRESS, 2, 0, BLOB_ADDRESS, 2, 0,
                                  tag=1, input=manifest.Tensor(1, 1, (1, 1, 1, 3), 3),
                                  outputs=(manifest.Tensor(2, 0, (4,), 4),), preprocessing=1, color=1)
        port = FakePort()
        upload.send_header(port, value.encode(), "model adopt", upload.time.monotonic() + 1)
        self.assertEqual(port.commands[-1], "model adopt")
        encoded = b"".join(bytes.fromhex(command.split()[-1]) for command in port.commands[:-1])
        self.assertEqual(encoded, value.encode())

    def test_image_and_output_files(self):
        try:
            import numpy as np
            from PIL import Image
        except ImportError:
            self.skipTest("install tool/requirements.txt for image tests")
        import tensor_io
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            path = root / "image.png"
            Image.new("RGB", (4, 2), (10, 20, 30)).save(path)
            value = manifest.Manifest(1201, 1, 12, 4, WEIGHTS_ADDRESS, 2, 0, BLOB_ADDRESS, 2, 0,
                                      tag=1, input=manifest.Tensor(2, 2, (1, 3, 2, 2), 12, scale=2, zero=-10),
                                      outputs=(manifest.Tensor(2, 0, (4,), 4, scale=0.5),), preprocessing=1, color=2)
            tensor, metadata = tensor_io.prepare_image(path, value)
            np.testing.assert_array_equal(np.frombuffer(tensor, dtype=np.int8).reshape(1, 3, 2, 2)[0, :, 0, 0], [5, 0, -5])
            payload = bytes([1, 2, 3, 4])
            tensor_io.save_result(root / "result", value.encode(), value, payload, zlib.crc32(payload), metadata)
            self.assertTrue((root / "result/tensors.npz").is_file())
            with self.assertRaises(ValueError):
                tensor_io.save_result(root / "bad", value.encode(), value, payload, 0)
            import dataclasses
            letterbox = dataclasses.replace(value, input=manifest.Tensor(1, 1, (1, 4, 4, 3), 48),
                                            input_bytes=48, color=1, preprocessing=2, padding=114)
            tensor, geometry = tensor_io.prepare_image(path, letterbox)
            pixels = np.frombuffer(tensor, dtype=np.uint8).reshape(4, 4, 3)
            np.testing.assert_array_equal(pixels[0, 0], [114, 114, 114])
            np.testing.assert_array_equal(pixels[1, 0], [10, 20, 30])
            self.assertEqual(geometry["padding"], [0, 1])


if __name__ == "__main__":
    unittest.main()
