"""Unified GUI regressions using real HTTP and the public CLI subprocesses."""

import base64
import json
import subprocess
import sys
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

from . import cli, gui


class UnifiedGuiTest(unittest.TestCase):
    def setUp(self):
        self.server = gui.create_server(gui.build_parser().parse_args(["--port", "0"]))
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}"
        self.token = self.get("/api/session")["token"]
        self.addCleanup(self.close)

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()
        self.server.session.temporary.cleanup()

    def get(self, path):
        with urllib.request.urlopen(self.url + path) as response:
            return json.load(response)

    def post(self, payload, token=None, path="/api/run", **headers):
        request = urllib.request.Request(
            self.url + path, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json", "X-Editor-Token": self.token if token is None else token, **headers},
        )
        with urllib.request.urlopen(request) as response:
            return json.load(response)

    def test_mounted_editors_preserve_api_and_distinct_sessions(self):
        layout = self.get("/ui/api/layout")
        memory = self.get("/memory/api/inputs")
        constraints = self.get("/constraints/api/document")
        self.assertEqual(len({self.token, layout["token"], memory["token"], constraints["token"]}), 4)
        self.assertTrue(self.post(layout["layout"], layout["token"], "/ui/api/validate")["ok"])
        result = self.post({"document": constraints["document"]}, constraints["token"], "/constraints/api/check")
        self.assertEqual(result["exit_code"], 0)
        self.assertIn("states", result["result"])
        for path in ("/ui/", "/memory/", "/constraints/"):
            with urllib.request.urlopen(self.url + path) as response:
                self.assertIn("frame-ancestors 'self'", response.headers["Content-Security-Policy"])
        for path in ("/memory/app.js", "/memory/style.css", "/constraints/app.js", "/lucide.min.js"):
            with urllib.request.urlopen(self.url + path) as response:
                self.assertGreater(len(response.read()), 100)

    def test_upload_runs_cli_and_artifacts_match_direct_decode(self):
        sample = gui.ROOT / "host_app/ai_model_monitor/sample/ai_model_monitor.bin"
        raw = sample.read_bytes()
        result = self.post({
            "tool": "ai-model-monitor",
            "arguments": ["decode", "{input}", "-o", "{output}/trace #1.json"],
            "upload": {"name": "capture.bin", "content": base64.b64encode(raw).decode()},
        })
        self.assertEqual(result["exit_code"], 0)
        direct = subprocess.run([sys.executable, "-m", "host_app", "ai-model-monitor", "decode", str(sample)],
                                cwd=gui.ROOT, capture_output=True, text=True, check=True)
        artifact = result["artifacts"][0]
        self.assertEqual(artifact["name"], "trace #1.json")
        self.assertEqual(self.get(artifact["url"]), json.loads(direct.stdout))
        self.assertEqual(sample.read_bytes(), raw)

    def test_cli_exit_code_stderr_and_quoted_arguments_are_preserved(self):
        result = self.post({"tool": "ui-designer", "arguments": "validate --layout 'missing file;echo nope.json'"})
        self.assertEqual(result["exit_code"], 2)
        self.assertIn("missing file;echo nope.json", result["stderr"])
        self.assertEqual(result["artifacts"], [])
        self.assertNotIn("\nnope", result["stdout"])

    def test_foreign_sessions_hosts_and_invalid_arguments_are_rejected(self):
        payload = {"tool": "ui-designer", "arguments": ["--help"]}
        for token, headers in (("invalid", {}), (self.token, {"Host": "foreign.example"})):
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.post(payload, token=token, **headers)
            self.assertEqual(caught.exception.code, 403)
            caught.exception.close()
        for invalid in (
            {"tool": "arbitrary-program", "arguments": []},
            {"tool": "ui-designer", "arguments": ["serve"]},
            {"tool": "ui-designer", "arguments": [42]},
            {"tool": "ai-model-monitor", "arguments": ["decode", "{input}"]},
        ):
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.post(invalid)
            self.assertEqual(caught.exception.code, 400)
            caught.exception.close()
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(self.url + "/artifacts/../../README.md")
        self.assertEqual(caught.exception.code, 403)
        caught.exception.close()

    def test_timeout_returns_error_without_success_result(self):
        with patch.object(gui, "CLI_TIMEOUT", 0.000001):
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.post({"tool": "ui-designer", "arguments": ["--help"]})
            self.assertEqual(caught.exception.code, 504)
            self.assertIn("exceeded", json.load(caught.exception)["error"])
            caught.exception.close()


class CliContractTest(unittest.TestCase):
    def test_every_tool_has_a_dependency_free_cli_help(self):
        for tool in cli.TOOLS:
            with self.subTest(tool=tool):
                result = subprocess.run([sys.executable, "-S", "-m", "host_app", tool, "--help"],
                                        cwd=gui.ROOT, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("usage:", result.stdout)

    def test_no_arguments_start_gui_without_mutating_process_arguments(self):
        original = list(sys.argv)
        with patch.object(gui, "main", return_value=0) as start:
            self.assertEqual(cli.main([]), 0)
            start.assert_called_once_with([])
        self.assertEqual(sys.argv, original)


if __name__ == "__main__":
    unittest.main()