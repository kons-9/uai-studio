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
import registry
from layout import BLOB_ADDRESS, BLOB_CAPACITY, WEIGHTS_ADDRESS, WEIGHTS_CAPACITY


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.weights = bytes(range(16))
        self.blob = bytes(range(8))
        self.value = manifest.Manifest(1201, 1, 192, 16, WEIGHTS_ADDRESS, 16, zlib.crc32(self.weights),
                                       BLOB_ADDRESS, 8, zlib.crc32(self.blob))

    def test_roundtrip_and_payload(self):
        self.assertEqual(manifest.decode(self.value.encode()), self.value)
        self.assertTrue(manifest.verify(self.value, self.weights, self.blob))
        self.assertFalse(manifest.verify(self.value, bytes(16), self.blob))
        damaged = bytearray(self.value.encode())
        damaged[16] ^= 1
        with self.assertRaises(ValueError):
            manifest.decode(damaged)

    def test_ranges(self):
        for changes in ({"weights_address": 0xfffffff8}, {"blob_address": WEIGHTS_ADDRESS + 8},
                        {"runtime_version": 0}, {"weights_bytes": 0}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                dataclasses.replace(self.value, **changes).encode()

    def test_tensor_header(self):
        inputs = manifest.Tensor(1, 1, (1, 8, 8, 3), 192, 0.5, 128)
        outputs = (manifest.Tensor(2, 0, (4,), 4, 0.25, -3), manifest.Tensor(3, 0, (4,), 16, offset=32))
        value = dataclasses.replace(self.value, tag=123, input=inputs, outputs=outputs, output_bytes=48,
                                    preprocessing=2, color=1)
        header = value.encode()
        self.assertEqual(len(header), 208)
        self.assertEqual(manifest.decode(header), value)
        executable = os.environ.get("MANIFEST_PROBE")
        if executable:
            with tempfile.TemporaryDirectory() as temporary:
                path = pathlib.Path(temporary) / "header.bin"
                path.write_bytes(header)
                decoded = subprocess.run([executable, "--header", str(path)], check=True, capture_output=True, text=True)
                self.assertEqual(decoded.stdout.strip(), "123 1 192 2 48")
                damaged = bytearray(header)
                damaged[120] = 1
                damaged[-4:] = manifest.CRC.pack(zlib.crc32(damaged[:-4]))
                path.write_bytes(damaged)
                self.assertNotEqual(subprocess.run([executable, "--header", str(path)], capture_output=True).returncode, 0)
        for changes in ({"tag": 0}, {"outputs": (dataclasses.replace(outputs[0], bytes=5),)},
                        {"divisor": (0, 1, 1)}, {"input": dataclasses.replace(inputs, zero=300)}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                dataclasses.replace(value, **changes).encode()

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
            self.assertEqual(value.weights_address, WEIGHTS_ADDRESS)
            self.assertEqual(value.blob_address, BLOB_ADDRESS)
            self.assertIn("expected_header[]", (output / "model_expected.hpp").read_text())
            with self.assertRaises(ValueError):
                model.package(generated, self.blob, generated)
            with self.assertRaises(ValueError):
                model.package(generated, bytes(BLOB_CAPACITY + 1), output)
            for changes in ({"input_bytes": 0x80001}, {"output_bytes": True}, {"kind": 0}, {"extra": 1}):
                (generated / "contract.json").write_text(json.dumps({**contract, **changes}))
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    model.package(generated, self.blob, output)

    def test_v3_contract_digest(self):
        inputs = manifest.Tensor(1, 1, (1, 8, 8, 3), 192)
        outputs = (manifest.Tensor(3, 0, (4,), 16),)
        value = dataclasses.replace(self.value, tag=123, input=inputs, outputs=outputs, contract_hash="ab" * 32)
        header = value.encode()
        self.assertEqual(header[4], 3)
        self.assertEqual(manifest.decode(header), value)
        executable = os.environ.get("MANIFEST_PROBE")
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "header.bin"
            maximum = dataclasses.replace(value, output_bytes=240,
                outputs=tuple(manifest.Tensor(3, 0, (4,), 16, offset=index * 32) for index in range(8)))
            self.assertEqual(len(maximum.encode()), 480)
            self.assertEqual(manifest.decode(maximum.encode()), maximum)
            path.write_bytes(maximum.encode())
            if executable:
                self.assertEqual(subprocess.run([executable, "--header", str(path)], capture_output=True).returncode, 0)
            path.write_bytes(header)
            if executable:
                self.assertEqual(subprocess.run([executable, "--header", str(path)], capture_output=True).returncode, 0)
            damaged = bytearray(header)
            damaged[-36:-4] = bytes(32)
            damaged[-4:] = manifest.CRC.pack(zlib.crc32(damaged[:-4]))
            path.write_bytes(damaged)
            with self.assertRaises(ValueError):
                manifest.decode(damaged)
            if executable:
                self.assertNotEqual(subprocess.run([executable, "--header", str(path)], capture_output=True).returncode, 0)
        for digest in ("00" * 32, "xy" * 32, "ab" * 31):
            with self.subTest(digest=digest), self.assertRaises(ValueError):
                dataclasses.replace(value, contract_hash=digest).encode()

    def test_three_model_catalog(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            packages = []
            for name, kind in model.KINDS.items():
                generated = root / name
                generated.mkdir()
                (generated / "contract.json").write_text(json.dumps({"runtime_version": 1201, "kind": kind,
                    "input": {"type": "uint8", "layout": "nhwc", "shape": [1, 8, 8, 3]},
                    "outputs": [{"type": "int8", "shape": [4]}, {"type": "float32", "shape": [4]}],
                    "preprocessing": 2, "color": 1}))
                (generated / "network.c").write_text(name)
                (generated / "weights.bin").write_bytes(self.weights)
                packaged = root / (name + "-package")
                value = model.package(generated, self.blob, packaged)
                self.assertEqual(value.kind, kind)
                self.assertEqual(value.output_bytes, 48)
                self.assertEqual(value.outputs[1].offset, 32)
                self.assertTrue(value.tag)
                packages.append(packaged)
            target = root / "catalog.hpp"
            subprocess.run([sys.executable, str(model.ROOT / "tool/model.py"), "catalog", "--packages",
                            *(str(path) for path in packages), "--output", str(target)], check=True)
            self.assertIn("catalog_count = 3", target.read_text())

    def test_npu_memory_pools(self):
        pools = json.loads((model.ROOT / "config" / "model.mpool").read_text())["memory"]["mempools"]
        self.assertEqual([int(pool["offset"]["value"], 0) for pool in pools],
                         [0x342e0000, 0x34350000, WEIGHTS_ADDRESS, 0x90400000])
        multipliers = {"BYTES": 1, "KBYTES": 1024, "MBYTES": 1024 * 1024}
        self.assertEqual([int(pool["size"]["value"]) * multipliers[pool["size"]["magnitude"]] for pool in pools],
                 [0x70000, 0x70000, WEIGHTS_CAPACITY, 0x800000])

    def test_slot_linker_script(self):
        if not all(shutil.which(tool) for tool in ("cc", "ld", "nm")):
            self.skipTest("native linker tools are unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for blob_bytes, succeeds in ((BLOB_CAPACITY, True), (BLOB_CAPACITY + 1, False)):
                source = f'''unsigned char weights[{WEIGHTS_CAPACITY}] __attribute__((section(".experiment_weights")));
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

    def test_three_blob_overlay(self):
        if not all(shutil.which(tool) for tool in ("cc", "ld", "nm")):
            self.skipTest("native linker tools are unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            names = (*model.KINDS, "detector2")
            registry.generate(names, root / "registry.hpp", root / "models.ld")
            source = f'void Reset_Handler(void) {{}}\nunsigned char weights[{WEIGHTS_CAPACITY}] __attribute__((section(".experiment_weights")));\n'
            for name in names:
                source += f'const unsigned char {name}[64] __attribute__((section(".model_blob_{name}"))) = {{1}};\n'
            subprocess.run(["cc", "-fno-asynchronous-unwind-tables", "-c", "-x", "c", "-", "-o", str(root / "models.o")],
                           input=source, text=True, check=True, capture_output=True)
            linked = subprocess.run(["ld", "-T", str(root / "models.ld"), "-T", str(model.ROOT / "slots.ld"),
                                     "-T", str(model.ROOT / "camera-runtime-ram.ld"), str(root / "models.o"),
                                     "-o", str(root / "models.elf")], capture_output=True, text=True)
            self.assertEqual(linked.returncode, 0, linked.stderr)
            symbols = subprocess.run(["nm", str(root / "models.elf")], check=True, capture_output=True, text=True).stdout
            for name in names:
                self.assertRegex(symbols, rf"(?m)^0*{BLOB_ADDRESS:x} [A-Z] {name}$")

    def test_three_model_cmake(self):
        if not shutil.which("cmake"):
            self.skipTest("CMake is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            sdk = root / "vendor"
            names = {**model.KINDS, "detector2": 1}
            for name, kind in names.items():
                generated = root / "models" / name
                generated.mkdir(parents=True)
                (generated / "contract.json").write_text(json.dumps({"runtime_version": 1201, "kind": kind,
                    "input": {"type": "uint8", "layout": "nhwc", "shape": [1, 2, 2, 3]},
                    "outputs": [{"type": "int8", "shape": [4]}, {"type": "float32", "shape": [4]}]}))
                for filename in ("network.c", "network_ecblobs.h", "stai_network.c", "stai_network.h", "weights.bin", "model_symbols.h"):
                    (generated / filename).write_text("")
            (root / "src").mkdir()
            for filename in ("npu_model.c", "npu_multi.cpp", "network_api.c"):
                shutil.copyfile(model.ROOT / "src" / filename, root / "src" / filename)
            (root / "tool").mkdir()
            for path in (model.ROOT / "tool").glob("*.py"):
                shutil.copyfile(path, root / "tool" / path.name)
            paths = ["Lib/GCC/ARMCortexM55/NetworkRuntime1201_CM55_GCC.a", "Src/stm32n6xx_hal_ramcfg.c",
                     "Npu/Devices/STM32N6xx/npu_cache.c", "Npu/Devices/STM32N6xx/mcu_cache.c"]
            paths += [f"Npu/ll_aton/{name}.c" for name in ("ecloader", "ll_aton", "ll_aton_cipher", "ll_aton_dbgtrc",
                      "ll_aton_lib", "ll_aton_lib_sw_operators", "ll_aton_runtime", "ll_aton_stai_internal",
                      "ll_aton_util", "ll_sw_float", "ll_sw_integer")]
            for name in paths:
                path = sdk / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("")
            (root / "dummy.c").write_text("int main(void) { return 0; }\n")
            (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
project(model_graph LANGUAGES C CXX)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(TARGET_NAME model_check)
add_executable(model_check dummy.c)
target_include_directories(model_check PRIVATE src)
target_compile_definitions(model_check PRIVATE GRAPH_TEST=1)
add_library(stm32n6570_dk INTERFACE)
set(MODEL_LOADER_MULTI ON)
set(MODEL_LOADER_MODELS "person;face;seg;detector2")
set(STEDGEAI_LIB_DIR "{sdk}")
set(HAL "{sdk}")
set(CUBE "{sdk}")
include("{model.ROOT / 'npu.cmake'}")
''')
            checked = subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build")], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            text = (root / "build/CMakeFiles/model_check.dir/build.make").read_text()
            self.assertIn("model_loader_model_face", text)
            self.assertIn("model_loader_model_seg", text)
            self.assertIn("model_loader_model_detector2", text)

    def test_strong_npu_link_audit(self):
        expected = {**check_link.EXPECTED, "NPU0_IRQHandler": "npu_model.c", "experiment_npu_start": "npu_model.c",
                "model_load_NPU0_IRQHandler": "ll_aton_runtime.c"}
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
