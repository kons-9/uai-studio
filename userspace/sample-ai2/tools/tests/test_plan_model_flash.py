"""Host-side regression tests for model binary layout and SVG reports."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

TOOL = Path(__file__).resolve().parents[1] / "plan_model_flash.py"
spec = importlib.util.spec_from_file_location("plan_model_flash", TOOL)
planner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(planner)


class ModelLayoutTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for filename, length in (("large.bin", 6000), ("small.bin", 1000)):
            (self.root / filename).write_bytes(b"a" * length)
        self.manifest_path = self.root / "layout.json"
        self.manifest = {
            "region": {"start": "0x10000", "size": "0x8000", "erase_size": 4096},
            "artifacts": [
                {"name": "large", "path": "large.bin"},
                {"name": "small", "path": "small.bin", "address": "0x13000"},
            ],
        }

    def test_measure_pin_pack_and_svg(self):
        result = planner.plan(self.manifest, self.manifest_path)
        placed = {x["name"]: x for x in result["placements"]}
        self.assertEqual(placed["large"]["address"], 0x10000)
        self.assertEqual(placed["large"]["size"], 6000)
        self.assertEqual(placed["large"]["end"], 0x12000)
        self.assertEqual(placed["small"]["address"], 0x13000)
        self.assertEqual(result["summary"]["free_bytes"], 0x5000)
        ET.fromstring(planner.draw_svg(result))

    def test_overlap_and_outside_rejected(self):
        self.manifest["artifacts"][0]["address"] = "0x10000"
        self.manifest["artifacts"][1]["address"] = "0x11000"
        with self.assertRaisesRegex(planner.LayoutError, "overlaps"):
            planner.plan(self.manifest, self.manifest_path)
        self.manifest["artifacts"][1]["address"] = "0x18000"
        with self.assertRaisesRegex(planner.LayoutError, "outside"):
            planner.plan(self.manifest, self.manifest_path)

    def test_reserved_region_and_unplaceable(self):
        self.manifest["reserved"] = [
            {"name": "boot", "address": "0x10000", "size": "0x2000"},
            {"name": "other", "address": "0x15000", "size": "0x2000"}]
        with self.assertRaisesRegex(planner.LayoutError, "no contiguous"):
            planner.plan(self.manifest, self.manifest_path)

    def test_alignment_and_duplicate(self):
        self.manifest["artifacts"][1]["alignment"] = 3000
        with self.assertRaisesRegex(planner.LayoutError, "power of two"):
            planner.plan(self.manifest, self.manifest_path)
        self.manifest["artifacts"][1]["alignment"] = 4096
        self.manifest["artifacts"][1]["name"] = "large"
        with self.assertRaisesRegex(planner.LayoutError, "duplicate name"):
            planner.plan(self.manifest, self.manifest_path)

    def test_cli_fail_closed_no_report_written(self):
        self.manifest["artifacts"][0]["address"] = "0x10000"
        self.manifest["artifacts"][1]["address"] = "0x11000"
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        destination = self.root / "invalid.json"
        run = subprocess.run([sys.executable, str(TOOL), str(self.manifest_path),
                              "--json", str(destination)], capture_output=True,
                             text=True, check=False)
        self.assertEqual(run.returncode, 2)
        self.assertIn("overlaps", run.stderr)
        self.assertFalse(destination.exists())

    def test_cli_creates_machine_and_visual_reports(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        destination = self.root / "valid.json"
        drawing = self.root / "valid.svg"
        run = subprocess.run([sys.executable, str(TOOL), str(self.manifest_path),
                              "--json", str(destination), "--svg", str(drawing)],
                             capture_output=True, text=True, check=False)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(destination.read_text())["summary"]
                         ["payload_bytes"], 7000)
        ET.parse(drawing)


if __name__ == "__main__":
    unittest.main()
