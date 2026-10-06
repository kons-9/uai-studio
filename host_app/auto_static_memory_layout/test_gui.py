"""Regression checks for the browser editor's real resolver and HTTP boundary."""

import copy
import io
import json
import threading
import unittest
import urllib.error
import urllib.request
import zipfile
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch

from .common import LayoutError
from .gui import Editor, make_handler
from .resolver import resolve_document


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "userspace/ai-app"


class EditorTest(unittest.TestCase):
    def setUp(self):
        self.editor = Editor(
            ROOT, APP / "config/board_memory.json", APP / "config/application_memory.json",
            APP / "models", APP / "config/model_layout.json", APP / "stm32n6570-dk-npu-ram.ld",
        )
        self.inputs = self.editor.inputs()
        model_loader = patch(
            "host_app.auto_static_memory_layout.resolver.resolve_models",
            return_value=[{"name": "fixture", "io": {"outputs": [{"size_bytes": 4096}]}}],
        )
        model_loader.start()
        self.addCleanup(model_loader.stop)

    def test_matches_cli_without_modifying_sources(self):
        originals = {name: path.read_bytes() for name, path in self.editor.paths.items()}
        result = self.editor.resolve(self.inputs)
        expected = resolve_document(
            ROOT, self.editor.paths["board"], self.editor.paths["application"],
            self.editor.models_dir, self.editor.paths["model_config"],
        )
        self.assertEqual(result["document"], expected)
        capture = next(region for region in result["regions"] if region["name"] == "PSRAM_CAPTURE")
        self.assertEqual(capture["used"], 2 * 1024**2)
        self.assertEqual(capture["allocations"][1]["offset"], 1024**2)
        for name, path in self.editor.paths.items():
            self.assertEqual(path.read_bytes(), originals[name])

    def test_edit_and_capacity_validation(self):
        self.inputs["application"]["runtime"]["capture"]["count"] = 1
        result = self.editor.resolve(self.inputs)
        capture = next(region for region in result["regions"] if region["name"] == "PSRAM_CAPTURE")
        self.assertEqual(capture["used"], 1024**2)
        self.inputs["application"]["runtime"]["capture"]["count"] = 3
        with self.assertRaisesRegex(LayoutError, "PSRAM_CAPTURE"):
            self.editor.resolve(self.inputs)

    def test_overlap_and_invalid_alignment(self):
        invalid = copy.deepcopy(self.inputs)
        invalid["board"]["memory_regions"][1]["origin"] = "0x34000400"
        with self.assertRaisesRegex(LayoutError, "overlap"):
            self.editor.resolve(invalid)
        self.inputs["application"]["alignment"] = 3
        with self.assertRaisesRegex(LayoutError, "power of two"):
            self.editor.resolve(self.inputs)

    def test_archive_contains_existing_artifacts_and_edited_inputs(self):
        self.inputs["application"]["runtime"]["capture"]["count"] = 1
        with zipfile.ZipFile(io.BytesIO(self.editor.export(self.inputs))) as archive:
            self.assertEqual(len(archive.namelist()), 9)
            self.assertEqual(json.loads(archive.read("config/application_memory.json")), self.inputs["application"])
            self.assertIn(b"kInference0", archive.read("middleware/memory/generated/static_memory_layout/key.hpp"))
            self.assertIn(b"MEMORY", archive.read("stm32n6570-dk-npu-ram.ld"))

    def test_http_rejects_foreign_session_and_bad_inputs(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.editor))
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}"
            with urllib.request.urlopen(url + "/api/inputs") as response:
                token = json.load(response)["token"]
            for headers, payload, status in (
                ({}, self.inputs, 403),
                ({"X-Editor-Token": token}, {}, 422),
                ({"X-Editor-Token": token, "Host": "foreign.example"}, self.inputs, 403),
            ):
                request = urllib.request.Request(url + "/api/resolve", data=json.dumps(payload).encode(), headers=headers)
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    urllib.request.urlopen(request)
                self.assertEqual(caught.exception.code, status)
                caught.exception.close()
            request = urllib.request.Request(url + "/api/resolve", data=json.dumps(self.inputs).encode(), headers={"X-Editor-Token": token})
            with urllib.request.urlopen(request) as response:
                self.assertIn("regions", json.load(response))
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    unittest.main()