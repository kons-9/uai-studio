"""Keep the host trace interpretation aligned with the runtime stages."""

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "visualize_thread_monitor.py"
SPEC = importlib.util.spec_from_file_location("visualize_thread_monitor", SCRIPT)
visualize = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(visualize)


class PipelineStageTest(unittest.TestCase):
    def test_epoch_continue_is_cpu_work_not_wait(self):
        self.assertIn("epoch_continue", visualize.CPU_ACTIVE_STAGE_IDS)
        self.assertNotIn("epoch_continue", visualize.CPU_WAIT_STAGE_IDS)
        self.assertIn("irq_wait", visualize.CPU_WAIT_STAGE_IDS)
        records = [
            {"type": "pipeline_stage", "pipeline_stage": "epoch_continue",
             "model_kind_id": 0, "stage_timing_valid": True,
             "stage_cycle_elapsed": 600000},
            {"type": "pipeline_stage", "pipeline_stage": "irq_wait",
             "model_kind_id": 0, "stage_timing_valid": True,
             "stage_cycle_elapsed": 6000000},
        ]
        self.assertAlmostEqual(
            visualize.average_decomposition(records, 600000000)[0]["cpu"],
            1.0)


if __name__ == "__main__":
    unittest.main()