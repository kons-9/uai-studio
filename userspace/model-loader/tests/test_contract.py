import copy
import dataclasses
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tool"))
import contract
import model
import manifest
import registry
import results
import tensor_io
import upload


class ContractTests(unittest.TestCase):
    def package(self, root, name="detector", semantics=None):
        source = root / name
        source.mkdir()
        (source / "contract.json").write_text(json.dumps({"runtime_version": 1201, "kind": 1,
            "input": {"type": "uint8", "layout": "nhwc", "shape": [1, 2, 2, 3]},
            "outputs": [{"type": "int8", "layout": "nchw", "shape": [1, 2, 2, 2], "scale": 0.1}]}))
        (source / "network.c").write_text("same compiled network")
        (source / "weights.bin").write_bytes(b"weights")
        if semantics is not None:
            (source / "semantics.json").write_text(json.dumps(semantics))
        destination = root / (name + "-package")
        model.package(source, b"commands", destination)
        return destination

    def test_v3_package_and_float32_normalization(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            description = contract.verify_package(destination / "manifest.bin")
            header = manifest.decode((destination / "manifest.bin").read_bytes())
            self.assertEqual(header.contract_hash, contract.digest(description))
            self.assertEqual(header.encode()[4], 3)
            self.assertEqual(description["decoder"]["id"], "raw")
            self.assertEqual(description["semantics"]["outputs"][0]["meaning"], "unknown")
            self.assertEqual(description["execution"]["outputs"][0]["scale"], header.outputs[0].scale)
            result = subprocess.run([sys.executable, str(model.ROOT / "tool/contract.py"), "verify", str(destination)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["status"], "verified")

    def test_semantic_tampering_and_missing_document(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            path = destination / "contract.json"
            description = json.loads(path.read_text())
            description["semantics"]["outputs"][0]["meaning"] = "class_logits"
            path.write_text(json.dumps(description))
            with self.assertRaisesRegex(ValueError, "do not match"):
                contract.verify_package(destination / "manifest.bin")
            path.unlink()
            with self.assertRaises(OSError):
                contract.verify_package(destination / "manifest.bin")

    def test_artifact_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            (destination / "weights.bin").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                contract.verify_package(destination / "manifest.bin")

    def test_structural_and_meaning_differences(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            before = contract.verify_package(destination / "manifest.bin")
            after = copy.deepcopy(before)
            after["semantics"]["labels"] = [{"id": 0, "name": "background"}]
            result = contract.compare(before, after)
            self.assertTrue(result["same_execution_contract"])
            self.assertFalse(result["same_meaning_contract"])
            self.assertIn("/labels", result["changes"]["semantics"])
            after = copy.deepcopy(before)
            after["execution"]["outputs"][0]["scale"] = 0.5
            result = contract.compare(before, after)
            self.assertFalse(result["same_execution_contract"])
            self.assertTrue(result["same_meaning_contract"])
            self.assertNotEqual(contract.digest(before), contract.digest(after))

    def test_unknown_schema_and_invalid_bindings(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            description = contract.verify_package(destination / "manifest.bin")
            for mutate in (
                lambda value: value.update(schema_version=2),
                lambda value: value["decoder"]["bindings"].update(scores="output9"),
                lambda value: value["semantics"]["outputs"][0].update(id="output9"),
                lambda value: value["semantics"].update(labels=[{"id": 0, "name": "a"}, {"id": 0, "name": "b"}])):
                invalid = copy.deepcopy(description)
                mutate(invalid)
                with self.assertRaises(ValueError):
                    contract.validate(invalid)

    def test_registry_names(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            names = ["person", "face", "seg", "detector2"]
            registry.generate(names, root / "registry.hpp", root / "models.ld")
            self.assertIn("registry_count = 4", (root / "registry.hpp").read_text())
            self.assertIn(".model_blob_detector2", (root / "models.ld").read_text())
            for invalid in ([], ["same", "same"], ["../outside"], ["a;bad"]):
                with self.assertRaises(ValueError):
                    registry.generate(invalid, root / "registry.hpp", root / "models.ld")

    def segmentation(self):
        return {"semantics": {"task": "semantic_segmentation", "input_domain": "prepared_tensor",
            "outputs": [{"id": "output0", "meaning": "class_logits", "axes": ["batch", "class", "y", "x"]}],
            "labels": [{"id": 0, "name": "background"}, {"id": 1, "name": "foreground"}]},
            "decoder": {"id": "segmentation.argmax", "version": 1, "bindings": {"scores": "output0"},
                        "parameters": {"class_axis": 1}, "result_schema": "segmentation/v1"}}

    def test_decoder_and_separate_policy(self):
        import numpy as np
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary), semantics=self.segmentation())
            document = contract.verify_package(destination / "manifest.bin")
            values = np.array([[[[1, 3], [5, 7]], [[2, 2], [6, 6]]]], dtype=np.int8)
            report, mask = results.decode_result(document, {"output0": values})
            np.testing.assert_array_equal(mask, [[1, 0], [1, 0]])
            policy = {"schema_version": 1, "id": "segmentation.area", "version": 1,
                      "parameters": {"class_id": 1, "minimum_fraction": 0.5}}
            positive = results.evaluate_policy(report, policy)
            self.assertTrue(positive["matched"])
            changed_policy = copy.deepcopy(policy)
            changed_policy["parameters"]["minimum_fraction"] = 0.75
            negative = results.evaluate_policy(report, changed_policy)
            self.assertFalse(negative["matched"])
            self.assertNotEqual(positive["policy_sha256"], negative["policy_sha256"])
            header = (destination / "manifest.bin").read_bytes()
            output = destination / "results"
            tensor_io.save_result(output, header, manifest.decode(header), values.tobytes(), zlib.crc32(values.tobytes()),
                                  document=document, policy=policy)
            saved = json.loads((output / "decoded.json").read_text())
            self.assertEqual(saved["status"], "decoded")
            self.assertTrue(saved["decision"]["matched"])
            self.assertEqual(saved["result"]["coordinate_space"], "model_output_grid")
            self.assertTrue((output / "classes.png").exists())
            raw_document = copy.deepcopy(document)
            raw_document["decoder"] = {"id": "raw", "version": 1, "bindings": {}, "parameters": {}, "result_schema": "raw_tensors/v1"}
            raw_model = dataclasses.replace(manifest.decode(header), contract_hash=contract.digest(raw_document))
            tensor_io.save_result(output, raw_model.encode(), raw_model, values.tobytes(), zlib.crc32(values.tobytes()),
                                  document=raw_document)
            self.assertEqual(json.loads((output / "decoded.json").read_text())["status"], "raw")
            self.assertFalse((output / "classes.png").exists())

    def test_raw_and_unknown_decoder_are_not_negative_decisions(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            document = contract.verify_package(destination / "manifest.bin")
            report, mask = results.decode_result(document, {})
            self.assertEqual(report["status"], "raw")
            self.assertIsNone(mask)
            policy = {"schema_version": 1, "id": "segmentation.area", "version": 1,
                      "parameters": {"class_id": 1, "minimum_fraction": 0.5}}
            self.assertNotIn("matched", results.evaluate_policy(report, policy))
            document["decoder"].update(id="unregistered.decoder", result_schema="custom/v1")
            report, mask = results.decode_result(document, {})
            self.assertEqual(report["status"], "unsupported")
            self.assertIsNone(mask)
            invalid = copy.deepcopy(policy)
            invalid["parameters"]["minimum_fraction"] = float("nan")
            with self.assertRaises(ValueError):
                results.validate_policy(invalid)

    def test_decoder_requires_semantic_axes_and_labels(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary), semantics=self.segmentation())
            document = contract.verify_package(destination / "manifest.bin")
            for mutate in (lambda value: value["semantics"]["outputs"][0].pop("axes"),
                           lambda value: value["semantics"].update(labels=[]),
                           lambda value: value["decoder"]["parameters"].update(class_axis=3)):
                invalid = copy.deepcopy(document)
                mutate(invalid)
                with self.assertRaises(ValueError):
                    results.validate_decoder(invalid)

    def test_upload_checks_semantics_before_opening_device(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = self.package(pathlib.Path(temporary))
            (destination / "contract.json").unlink()
            with self.assertRaises(OSError):
                upload.read_payload(destination / "manifest.bin", destination / "weights.bin", destination / "blob.bin")

    def test_host_entrypoint_and_offline_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            destination = self.package(root)
            source = root / "result.bin"
            source.write_bytes(bytes(8))
            output = root / "results"
            checked = subprocess.run([sys.executable, "-m", "host_app", "model-loader", "result", "--package", str(destination),
                "--input", str(source), "--output", str(output)], cwd=model.ROOT.parents[1], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(json.loads((output / "decoded.json").read_text())["status"], "raw")
            self.assertFalse((output / "classes.png").exists())

    def test_real_multi_backend_selects_contract_not_kind(self):
        if not sys.platform.startswith("linux") or not shutil.which("c++"):
            self.skipTest("native Linux backend probe requires c++ and mmap")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            names = ["first", "second", "third", "fourth"]
            packages = [self.package(root, name) for name in names]
            subprocess.run([sys.executable, str(model.ROOT / "tool/model.py"), "catalog", "--packages",
                *(str(package) for package in packages), "--output", str(root / "model_expected.hpp")], check=True)
            registry.generate(names, root / "model_registry.hpp", root / "models.ld")
            (root / "stm32n6xx_hal.h").write_text('''#pragma once
#include <stdint.h>
#define NPU0_IRQn 0
#define __DSB() ((void)0)
static inline void SCB_CleanDCache_by_Addr(void *, uint32_t) {}
static inline void SCB_CleanInvalidateDCache_by_Addr(void *, uint32_t) {}
static inline void SCB_InvalidateDCache_by_Addr(void *, uint32_t) {}
static inline void HAL_NVIC_EnableIRQ(int) {}
static inline uint32_t HAL_GetTick() { return 0; }
''')
            (root / "stm32n6570_discovery_xspi.h").write_text('''#pragma once
#define BSP_ERROR_NONE 0
static inline int BSP_XSPI_RAM_Init(unsigned) { return 0; }
static inline int BSP_XSPI_RAM_EnableMemoryMappedMode(unsigned) { return 0; }
''')
            source = '''#include "npu_multi.hpp"
#include "model_expected.hpp"
#include <cassert>
#include <sys/mman.h>
int selected_index = -1;
extern "C" bool experiment_npu_prepare() { return true; }
extern "C" bool experiment_npu_stop() { return true; }
extern "C" void experiment_npu_registered() {}
extern "C" void experiment_npu_completed() {}
'''
            for index, name in enumerate(names):
                source += f'''extern "C" bool experiment_{name}_start(std::uint8_t *, std::uint32_t, std::uint8_t **, std::uint32_t) {{ selected_index = {index}; return true; }}
extern "C" int experiment_{name}_poll() {{ return 1; }}
extern "C" bool experiment_{name}_deinit() {{ return true; }}
'''
            source += '''int main() {
    void *memory = mmap(reinterpret_cast<void *>(0x90c00000), 0x600000, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(memory != MAP_FAILED);
    experiment::model::Manifest selected{};
    assert(experiment::model::Decode(catalog_headers[3], catalog_sizes[3], selected));
    assert(selected.kind == 1);
    experiment::model::MultiBackend backend;
    assert(backend.Start(selected));
    assert(selected_index == 3);
    assert(backend.Poll() == experiment::model::Progress::kDone);
    std::size_t bytes = 0;
    assert(backend.Output(bytes) != nullptr && bytes == 8);
    assert(backend.Stop());
    selected.contract_digest[0] ^= 1;
    assert(!backend.Start(selected));
    munmap(memory, 0x600000);
}
'''
            (root / "probe.cpp").write_text(source)
            compiled = subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DPIPE2_BUFFER_PSRAM=0",
                "-I" + str(root), "-I" + str(model.ROOT / "src"), str(root / "probe.cpp"),
                str(model.ROOT / "src/npu_multi.cpp"), "-o", str(root / "probe")], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(subprocess.run([str(root / "probe")], capture_output=True).returncode, 0)
            firmware = subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DMODEL_LOADER_NPU=1",
                "-DMODEL_LOADER_MULTI=1", "-DPIPE2_BUFFER_PSRAM=0", "-I" + str(root), "-I" + str(model.ROOT / "src"),
                "-I" + str(model.ROOT / "src/camera_runtime"), "-fsyntax-only", str(model.ROOT / "src/firmware.cpp")],
                capture_output=True, text=True)
            self.assertEqual(firmware.returncode, 0, firmware.stderr)


if __name__ == "__main__":
    unittest.main()