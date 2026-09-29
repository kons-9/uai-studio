"""Fail if fixed STM32N6 PSRAM reservations overlap or exceed the aperture."""

from pathlib import Path
import re
import unittest

LINKER = Path(__file__).resolve().parents[2] / "stm32n6570-dk-npu-ram.ld"
MEMORY = re.compile(r"^\s*(PSRAM_[A-Z0-9_]+) \(rwx\) : ORIGIN = "
                    r"(0x[0-9A-Fa-f]+), LENGTH = (0x[0-9A-Fa-f]+)$", re.M)


class PsramLayoutTest(unittest.TestCase):
    def test_regions_are_disjoint_and_within_psram(self):
        text = LINKER.read_text(encoding="utf-8")
        regions = [(int(origin, 16), int(origin, 16) + int(size, 16), name)
                   for name, origin, size in MEMORY.findall(text)]
        self.assertGreaterEqual(len(regions), 9)
        for (start, end, name), (next_start, _, next_name) in zip(
                sorted(regions), sorted(regions)[1:]):
            self.assertLessEqual(end, next_start, f"{name} overlaps {next_name}")
        self.assertEqual(min(start for start, _, _ in regions), 0x91000000)
        self.assertEqual(max(end for _, end, _ in regions), 0x92000000)

    def test_extra_slots_have_separate_source_guard_space(self):
        text = LINKER.read_text(encoding="utf-8")
        self.assertIn("__sample_ai_inference4_start__", text)
        self.assertIn("__sample_ai_inference_source4_start__", text)
        # The source payload is 0xA8C00 bytes; leave one 32-byte cache
        # line per snapshot for the overflow sentinel.
        self.assertEqual(len(re.findall(r"\. = \. \+ 0x000A8C20;", text)), 5)


if __name__ == "__main__":
    unittest.main()
