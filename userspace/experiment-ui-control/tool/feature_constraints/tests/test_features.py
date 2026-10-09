import copy
import http.client
import json
import pathlib
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
EXAMPLE = REPO_ROOT / "config/features.json"
sys.path.insert(0, str(REPO_ROOT))
from tool.feature_constraints import cli
from tool.feature_constraints import gui


class FeatureTests(unittest.TestCase):
    def setUp(self):
        self.document = json.loads(EXAMPLE.read_text(encoding="utf-8"))

    def test_reachable_and_generated_execution(self):
        model = cli.explore(self.document)
        self.assertEqual(model["states"], [("camera", "on"), ("camera", "off"), ("pipe2", "off")])
        self.assertEqual(model["edges"], [[1, -1, 0], [0, 2, 1], [-1, 2, 1]])
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "features.hpp").write_text(cli.emit(model))
            (root / "check.cpp").write_text('''#include "features.hpp"
int main() {
    for (unsigned state = 0; state < 4; ++state) {
        for (unsigned action = 0; action < 4; ++action) {
            const int expected[3][3] = {{1,-1,0},{0,2,1},{-1,2,1}};
            experiment::features::State current{state};
            const bool allowed = state < 3 && action < 3 && expected[state][action] >= 0;
            const auto selected = static_cast<experiment::features::Action>(action);
            if (experiment::features::IsEnabled(current, selected) != allowed) return 1;
            if (experiment::features::Apply(current, selected) != allowed) return 2;
            if (current.index != (allowed ? static_cast<unsigned>(expected[state][action]) : state)) return 3;
        }
    }
}
''')
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(root / "check.cpp"), "-o", str(root / "check")], check=True)
            subprocess.run([str(root / "check")], check=True)

    def test_generated_values_preserve_utf8_and_control_bytes(self):
        values = ["camera", "\u30ab\u30e1\u30e9", "\U0001f7e2", '"\\??/\n\t\x00\x01\x7f9a']
        document = {"features": [{"id": "mode", "states": values, "initial": values[0]}],
                    "actions": [{"id": "Next", "when": {"mode": value},
                                 "set": {"mode": values[(index + 1) % len(values)]}}
                                for index, value in enumerate(values)]}
        checks = []
        for index, value in enumerate(values):
            expected = ",".join(map(str, value.encode("utf-8")))
            checks.append(f"{{ const unsigned char expected[] = {{{expected}}}; "
                          f"const auto *actual = experiment::features::Value({{{index}}}, 0); "
                          f"if (std::memcmp(actual, expected, sizeof(expected)) || actual[sizeof(expected)] != 0) return {index + 1}; }}")
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "features.hpp").write_text(cli.emit(cli.explore(document)), encoding="ascii")
            (root / "check.cpp").write_text('#include "features.hpp"\n#include <cstring>\nint main() {\n' +
                                            "\n".join(checks) + "\n}\n", encoding="ascii")
            subprocess.run(["c++", "-std=c++17", "-trigraphs", "-Wall", "-Wextra", "-Werror",
                            str(root / "check.cpp"), "-o", str(root / "check")], check=True)
            subprocess.run([str(root / "check")], check=True)

    def test_counterexample(self):
        self.document["actions"][2].pop("when")
        with self.assertRaisesRegex(ValueError, "counterexample: ShowPipe2"):
            cli.explore(self.document)

    def test_ambiguous_and_unknown_and_limit(self):
        duplicate = copy.deepcopy(self.document)
        duplicate["actions"].append(duplicate["actions"][0])
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            cli.explore(duplicate)
        with self.assertRaisesRegex(ValueError, "limit"):
            cli.explore(self.document, limit=3)
        self.document["actions"][0]["set"]["unknown"] = "off"
        with self.assertRaisesRegex(ValueError, "unknown"):
            cli.explore(self.document)

    def test_liveness(self):
        self.document["liveness"] = [{"reachable": {"display": "pipe2", "boxes": "on"}}]
        with self.assertRaisesRegex(ValueError, "unreachable"):
            cli.explore(self.document)

    def test_simulation_commits_candidate_and_reports_guards(self):
        result = cli.simulate(self.document, ["ToggleBoxes", "ShowPipe2"])
        self.assertEqual(result["status"], "committed")
        self.assertEqual(result["state"], {"display": "pipe2", "boxes": "off"})
        self.assertNotIn("ToggleBoxes", result["enabled"])
        self.assertEqual(result["blocked"]["ToggleBoxes"], [
            {"display": "camera", "boxes": "on"}, {"display": "camera", "boxes": "off"}])

    def test_simulation_rejects_without_partial_commit(self):
        result = cli.simulate(self.document, ["ToggleBoxes", "ShowPipe2", "ToggleBoxes"])
        self.assertEqual(result["status"], "rejected")
        self.assertEqual(result["state"], {"display": "camera", "boxes": "on"})
        self.assertEqual(result["steps"][-1]["candidate"], {"display": "pipe2", "boxes": "off"})
        self.assertFalse(result["steps"][-1]["accepted"])
        self.assertNotIn("ShowPipe2", result["enabled"])
        with self.assertRaisesRegex(ValueError, "unknown action"):
            cli.simulate(self.document, ["Unknown"])

    def test_simulation_cli(self):
        command = [sys.executable, "-m", "tool.feature_constraints", "simulate", str(EXAMPLE)]
        ready = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(ready.stdout)["status"], "idle")
        committed = subprocess.run(command + ["--actions", "ToggleBoxes", "ShowPipe2"],
                                   cwd=REPO_ROOT, capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(committed.stdout)["status"], "committed")
        rejected = subprocess.run(command + ["--actions", "ShowPipe2"], cwd=REPO_ROOT, capture_output=True, text=True)
        self.assertEqual(rejected.returncode, 2)
        self.assertEqual(json.loads(rejected.stdout)["steps"][0]["requires_one_of"], [{"boxes": "off"}])

    def test_cli_standard_streams(self):
        command = [sys.executable, "-m", "tool.feature_constraints"]
        header = subprocess.run(command + ["generate", "-", "--output", "-"], input=json.dumps(self.document),
                                cwd=REPO_ROOT, capture_output=True, text=True, check=True)
        self.assertEqual(header.stdout, cli.emit(cli.explore(self.document)))
        invalid = subprocess.run(command + ["check", "-"], input="{", cwd=REPO_ROOT, capture_output=True, text=True)
        self.assertEqual(invalid.returncode, 1)
        self.assertEqual(invalid.stdout, "")
        self.assertNotIn("Traceback", invalid.stderr)

    def test_schema_errors_are_cli_diagnostics(self):
        documents = [[], {**self.document, "features": "wrong"}, {**self.document, "invariants": {}},
                     {**self.document, "liveness": [{}]}, {**self.document, "actions": {}}]
        malformed = copy.deepcopy(self.document)
        malformed["features"][0]["states"] = "camera"
        documents.append(malformed)
        for document in documents:
            for command in ("check", "simulate"):
                with self.subTest(document=document, command=command):
                    result = subprocess.run([sys.executable, "-m", "tool.feature_constraints", command, "-"],
                                            input=json.dumps(document), cwd=REPO_ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 1)
                    self.assertNotIn("Traceback", result.stderr)


class MatrixTests(unittest.TestCase):
    def setUp(self):
        self.document = {"schema_version": 2, "features": [
            {"id": name, "states": ["off", "on"], "initial": "off"} for name in ("alpha", "beta")],
            "exclusions": [], "requirements": []}
        self.initial = {"alpha": "off", "beta": "off"}
        self.pair = [{"feature": "alpha", "state": "on"}, {"feature": "beta", "state": "on"}]

    def test_exclusion_is_symmetric_and_rejection_keeps_state(self):
        self.document["exclusions"] = [self.pair]
        for name, other in (("alpha", "beta"), ("beta", "alpha")):
            current = {**self.initial, name: "on"}
            result = cli.evaluate(self.document, current, {"feature": other, "state": "on"})
            self.assertEqual(result["status"], "rejected")
            self.assertEqual(result["state"], current)
            self.assertEqual(result["violations"][0]["kind"], "exclusion")
            self.assertEqual(result["violations"][0]["conditions"], {"alpha": "on", "beta": "on"})
        self.document["exclusions"].append(list(reversed(self.pair)))
        self.assertEqual(len(cli.explore(self.document)["invariants"]), 1)

    def test_prerequisite_is_directed_and_provider_cannot_be_disabled(self):
        self.document["requirements"] = [self.pair]
        denied = cli.evaluate(self.document, self.initial, {"feature": "alpha", "state": "on"})
        self.assertEqual(denied["status"], "rejected")
        self.assertEqual(denied["state"], self.initial)
        provider = cli.evaluate(self.document, self.initial, {"feature": "beta", "state": "on"})
        self.assertEqual(provider["status"], "accepted")
        dependent = cli.evaluate(self.document, provider["state"], {"feature": "alpha", "state": "on"})
        self.assertEqual(dependent["status"], "accepted")
        denied = cli.evaluate(self.document, dependent["state"], {"feature": "beta", "state": "off"})
        self.assertEqual(denied["status"], "rejected")
        self.assertEqual(denied["state"], dependent["state"])
        self.assertEqual(denied["violations"][0]["needs"], {"beta": "on"})

    def test_initial_state_invalid_data_and_limits(self):
        self.assertEqual(len(cli.explore(self.document)["states"]), 4)
        with self.assertRaisesRegex(ValueError, "limit"):
            cli.explore(self.document, limit=3)
        self.document["exclusions"] = [self.pair]
        for feature in self.document["features"]:
            feature["initial"] = "on"
        with self.assertRaisesRegex(ValueError, "initial state"):
            cli.explore(self.document)
        self.document["features"][0]["initial"] = "off"
        self.document["exclusions"][0][0]["state"] = "missing"
        with self.assertRaisesRegex(ValueError, "unknown feature or state"):
            cli.explore(self.document)

    def test_cycles_report_unreachable_modes_without_automatic_enabling(self):
        self.document["requirements"] = [self.pair, list(reversed(self.pair))]
        model = cli.explore(self.document)
        self.assertEqual(len(model["states"]), 1)
        self.assertEqual(model["warnings"], ["Unreachable: alpha=on", "Unreachable: beta=on"])
        with self.assertRaisesRegex(ValueError, "unreachable"):
            cli.evaluate(self.document, {"alpha": "on", "beta": "on"}, {"feature": "alpha", "state": "off"})
        with self.assertRaisesRegex(ValueError, "current state"):
            cli.evaluate(self.document, {"alpha": "off"}, {"feature": "alpha", "state": "on"})

    def test_generated_setters_match_cli_for_every_reachable_state(self):
        self.document["features"][1]["states"].append("standby")
        self.document["requirements"] = [self.pair]
        model = cli.explore(self.document)
        expected = []
        for state in model["states"]:
            row = []
            current = dict(zip(model["features"], state))
            for feature in self.document["features"]:
                for value_index in range(3):
                    if value_index >= len(feature["states"]):
                        row.append(-1)
                        continue
                    result = cli.evaluate(self.document, current, {"feature": feature["id"], "state": feature["states"][value_index]})
                    destination = tuple(result["state"][name] for name in model["features"])
                    row.append(model["states"].index(destination) if result["status"] == "accepted" else -1)
            expected.append("{" + ",".join(map(str, row)) + "}")
        program = '#include "features.hpp"\nint main() {\nconst int expected[][6] = {' + ",".join(expected) + '''};
    for (unsigned row = 0; row < sizeof(expected) / sizeof(expected[0]); ++row) {
        for (unsigned feature = 0; feature < 2; ++feature) {
            for (unsigned value = 0; value < 3; ++value) {
                experiment::features::State state{row};
                const auto action = experiment::features::SetAction(feature, value);
                const int next = expected[row][feature * 3 + value];
                if (experiment::features::Apply(state, action) != (next >= 0)) return 1;
                if (state.index != (next >= 0 ? static_cast<unsigned>(next) : row)) return 2;
            }
        }
    }
    if (experiment::features::IsEnabled({}, experiment::features::SetAction(99, 0))) return 3;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "features.hpp").write_text(cli.emit(model), encoding="ascii")
            (root / "check.cpp").write_text(program, encoding="ascii")
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(root / "check.cpp"), "-o", str(root / "check")], check=True)
            subprocess.run([str(root / "check")], check=True)

    def test_evaluate_cli_preserves_rejection_result_on_stdout(self):
        self.document["requirements"] = [self.pair]
        payload = {"document": self.document, "current": self.initial, "change": self.pair[0]}
        result = subprocess.run([sys.executable, "-m", "tool.feature_constraints", "evaluate", "-"],
                                input=json.dumps(payload), cwd=REPO_ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(json.loads(result.stdout)["state"], self.initial)
        self.assertEqual(result.stderr, "")


class GuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = gui.create_server(EXAMPLE)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_address[1]
        connection = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=5)
        connection.request("GET", "/api/document")
        cls.bootstrap = json.loads(connection.getresponse().read())
        connection.close()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def post(self, command, document=None, actions=None, token=True, current=None, change=None):
        payload = {"document": self.bootstrap["document"] if document is None else document,
                   "actions": [] if actions is None else actions}
        if command == "evaluate":
            payload.update(current=current, change=change)
        headers = {"Content-Type": "application/json"}
        if token:
            headers["X-Editor-Token"] = self.bootstrap["token"]
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        connection.request("POST", f"/api/{command}", json.dumps(payload), headers)
        response = connection.getresponse()
        body = json.loads(response.read())
        connection.close()
        return response.status, body

    def test_gui_calls_cli_with_unsaved_document(self):
        document = copy.deepcopy(self.bootstrap["document"])
        document["features"][1]["initial"] = "off"
        with mock.patch.object(gui.subprocess, "run", wraps=subprocess.run) as invocation:
            status, payload = self.post("check", document=document)
        self.assertEqual(status, 200)
        self.assertEqual(payload["result"]["states"][0], ["camera", "off"])
        arguments, options = invocation.call_args
        self.assertEqual(arguments[0][:5], [sys.executable, "-m", "tool.feature_constraints", "check", "-"])
        self.assertEqual(json.loads(options["input"]), document)
        self.assertNotIn("shell", options)
        self.assertEqual(json.loads(EXAMPLE.read_text())["features"][1]["initial"], "on")

    def test_gui_simulation_and_generation(self):
        status, result = self.post("simulate", actions=["ToggleBoxes", "ShowPipe2"])
        self.assertEqual(status, 200)
        self.assertEqual(result["result"]["state"], {"display": "pipe2", "boxes": "off"})
        status, result = self.post("simulate", actions=["ShowPipe2"])
        self.assertEqual(status, 200)
        self.assertEqual(result["exit_code"], 2)
        self.assertEqual(result["result"]["status"], "rejected")
        status, result = self.post("generate")
        self.assertEqual(status, 200)
        self.assertEqual(result["result"], cli.emit(cli.explore(self.bootstrap["document"])))

    def test_cli_errors_and_timeout_are_reported(self):
        document = copy.deepcopy(self.bootstrap["document"])
        document["actions"][2].pop("when")
        status, result = self.post("check", document=document)
        self.assertEqual(status, 422)
        self.assertIn("counterexample: ShowPipe2", result["error"])
        with mock.patch.object(gui.subprocess, "run", side_effect=subprocess.TimeoutExpired("cli", gui.CLI_TIMEOUT)):
            status, result = self.post("check")
        self.assertEqual(status, 504)
        self.assertIn("CLI exceeded", result["error"])

    def test_gui_evaluation_uses_cli_and_returns_expected_rejection(self):
        document = json.loads((REPO_ROOT / "tool/feature_constraints/example.json").read_text())
        current = {feature["id"]: feature["initial"] for feature in document["features"]}
        with mock.patch.object(gui.subprocess, "run", wraps=subprocess.run) as invocation:
            status, result = self.post("evaluate", document=document, current=current,
                                       change={"feature": "ai", "state": "on"})
        self.assertEqual(status, 200)
        self.assertEqual(result["exit_code"], 2)
        self.assertEqual(result["result"]["state"], current)
        self.assertIn("needs camera=on", result["result"]["violations"][0]["message"])
        self.assertEqual(json.loads(invocation.call_args.kwargs["input"])["current"], current)
        status, result = self.post("evaluate", document=document, current=current,
                                  change={"feature": "camera", "state": "on"})
        self.assertEqual(status, 200)
        self.assertEqual(result["result"]["status"], "accepted")
        self.assertEqual(result["result"]["state"]["camera"], "on")

    def test_session_and_cli_option_injection_are_rejected(self):
        with mock.patch.object(gui.subprocess, "run") as invocation:
            self.assertEqual(self.post("check", token=False)[0], 403)
            self.assertEqual(self.post("simulate", actions=["--output"])[0], 400)
            self.assertEqual(self.post("unknown")[0], 404)
            invocation.assert_not_called()
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        connection.request("GET", "/api/document", headers={"Host": "remote.example"})
        self.assertEqual(connection.getresponse().status, 403)
        connection.close()


if __name__ == "__main__":
    unittest.main()