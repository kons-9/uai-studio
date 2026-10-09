import dataclasses
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import manifest


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.weights = bytes(range(16))
        self.blob = bytes(range(8))
        self.value = manifest.Manifest(1201, 1, 192, 16, 0x91000000, 16, zlib.crc32(self.weights), 0x91000100, 8, zlib.crc32(self.blob))

    def test_roundtrip_and_payload(self):
        self.assertEqual(manifest.decode(self.value.encode()), self.value)
        self.assertTrue(manifest.verify(self.value, self.weights, self.blob))
        self.assertFalse(manifest.verify(self.value, bytes(16), self.blob))
        damaged = bytearray(self.value.encode())
        damaged[16] ^= 1
        with self.assertRaises(ValueError):
            manifest.decode(damaged)

    def test_ranges(self):
        for changes in ({"weights_address": 0xfffffff8}, {"blob_address": 0x91000008}, {"runtime_version": 0}, {"weights_bytes": 0}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                dataclasses.replace(self.value, **changes).encode()

    def test_cpp_contract(self):
        executable = os.environ.get("MANIFEST_PROBE")
        if not executable:
            self.skipTest("use CTest to run the C++ cross-language check")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            data, weights, blob = root / "manifest.bin", root / "weights.bin", root / "blob.bin"
            weights.write_bytes(self.weights)
            blob.write_bytes(self.blob)
            for value, expected in ((self.value, 0), (dataclasses.replace(self.value, runtime_version=1202), 1),
                                    (dataclasses.replace(self.value, input_bytes=193), 1),
                                    (dataclasses.replace(self.value, weights_address=0x90000000), 1)):
                data.write_bytes(value.encode())
                self.assertEqual(subprocess.run([executable, str(data), str(weights), str(blob)]).returncode, expected)
            data.write_bytes(self.value.encode())
            weights.write_bytes(bytes(16))
            self.assertEqual(subprocess.run([executable, str(data), str(weights), str(blob)]).returncode, 1)
            weights.write_bytes(self.weights)
            data.write_bytes(self.value.encode()[:-1])
            self.assertEqual(subprocess.run([executable, str(data), str(weights), str(blob)]).returncode, 1)


if __name__ == "__main__":
    unittest.main()