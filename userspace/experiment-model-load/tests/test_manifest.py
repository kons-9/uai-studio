import dataclasses
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tool"))
import manifest
import model
import check_link


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

    def test_generated_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            generated, output = root / "generated", root / "package"
            generated.mkdir()
            contract = dict(runtime_version=1201, kind=1, input_bytes=192, output_bytes=16)
            (generated / "contract.json").write_text(json.dumps(contract))
            (generated / "weights.bin").write_bytes(self.weights)
            value = model.package(generated, self.blob, output)
            self.assertEqual(manifest.decode((output / "manifest.bin").read_bytes()), value)
            self.assertTrue(manifest.verify(value, (output / "weights.bin").read_bytes(), (output / "blob.bin").read_bytes()))
            self.assertEqual(value.weights_address, 0x91010000)
            self.assertEqual(value.blob_address, 0x91020000)
            self.assertIn("expected_header[]", (output / "model_expected.hpp").read_text())
            with self.assertRaises(ValueError):
                model.package(generated, self.blob, generated)
            with self.assertRaises(ValueError):
                model.package(generated, bytes(0x10001), output)
            for changes in ({"input_bytes": 0x10001}, {"output_bytes": True}, {"kind": 0}, {"extra": 1}):
                (generated / "contract.json").write_text(json.dumps({**contract, **changes}))
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    model.package(generated, self.blob, output)

    def test_npu_memory_pools(self):
        pools = json.loads((model.ROOT / "config" / "model.mpool").read_text())["memory"]["mempools"]
        self.assertEqual([int(pool["offset"]["value"], 0) for pool in pools], [0x342e0000, 0x34350000, 0x91010000])
        self.assertEqual([int(pool["size"]["value"]) * 1024 for pool in pools], [0x70000, 0x70000, 0x10000])

    def test_slot_linker_script(self):
        if not all(shutil.which(tool) for tool in ("cc", "ld", "nm")):
            self.skipTest("native linker tools are unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for blob_bytes, succeeds in ((0x10000, True), (0x10001, False)):
                source = f'''unsigned char weights[65536] __attribute__((section(".experiment_weights")));
unsigned char blob[{blob_bytes}] __attribute__((section(".experiment_blob")));
void Reset_Handler(void) {{}}
'''
                subprocess.run(["cc", "-fno-asynchronous-unwind-tables", "-c", "-x", "c", "-", "-o", str(root / "slots.o")],
                               input=source, text=True, check=True, capture_output=True)
                linked = subprocess.run(["ld", "-T", str(model.ROOT / "slots.ld"), "-T", str(model.ROOT / "camera-runtime-ram.ld"),
                                         str(root / "slots.o"), "-o", str(root / "slots.elf")], capture_output=True, text=True)
                if succeeds:
                    self.assertEqual(linked.returncode, 0, linked.stderr)
                    symbols = subprocess.run(["nm", "--defined-only", str(root / "slots.elf")],
                                             check=True, capture_output=True, text=True).stdout
                    check_link.audit_slots(symbols)
                else:
                    self.assertNotEqual(linked.returncode, 0)

    def test_command_blob_object_package(self):
        if not all(shutil.which(tool) for tool in ("cc", "objcopy", "objdump")):
            self.skipTest("native object tools are unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            generated, output = root / "generated", root / "package"
            generated.mkdir()
            (generated / "contract.json").write_text(json.dumps(dict(runtime_version=1201, kind=1, input_bytes=4, output_bytes=4)))
            (generated / "weights.bin").write_bytes(b"AB")
            for source, succeeds in ((
                    'const unsigned char commands[] __attribute__((section(".model_command_blob"))) = {1,2,3,4};', True), (
                    'extern int target; const void *commands __attribute__((section(".model_command_blob"))) = &target;', False)):
                subprocess.run(["cc", "-c", "-x", "c", "-", "-o", str(root / "model.o")], input=source, text=True,
                               capture_output=True, check=True)
                packed = subprocess.run([sys.executable, str(model.ROOT / "tool" / "model.py"), "package", "--model-dir", str(generated),
                                         "--object", str(root / "model.o"), "--objcopy", "objcopy", "--objdump", "objdump",
                                         "--output-dir", str(output)], capture_output=True, text=True)
                if succeeds:
                    self.assertEqual(packed.returncode, 0, packed.stderr)
                    self.assertEqual((output / "blob.bin").read_bytes(), bytes([1, 2, 3, 4]))
                else:
                    self.assertNotEqual(packed.returncode, 0)
                    self.assertIn("link relocations", packed.stderr)

    def test_strong_npu_link_audit(self):
        expected = {**check_link.EXPECTED, "NPU0_IRQHandler": "npu_model.c", "experiment_npu_start": "npu_model.c"}
        symbols, placements = [], []
        for index, (symbol, source) in enumerate(expected.items()):
            address = 0x34001000 + index * 32
            symbols.append(f"{address:08x} T {symbol}")
            placements.append(f".text.{symbol} 0x{address:x} 0x20 local/{source}.o")
        symbol_text, map_text = "\n".join(symbols), "\n".join(placements)
        check_link.audit(symbol_text, map_text, npu=True)
        with self.assertRaises(ValueError):
            check_link.audit(symbol_text.replace("T NPU0_IRQHandler", "W NPU0_IRQHandler"), map_text, npu=True)
        with self.assertRaises(ValueError):
            check_link.audit(symbol_text, map_text.replace("local/npu_model.c.o", "other/fallback.c.o"), npu=True)

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