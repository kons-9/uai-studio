import unittest
import json
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from dataclasses import asdict

from decode_uart_trace import decode
from decode_thread_monitor import decode as decode_ai
from host_app.cpu_task_monitor.cpu_task_monitor import parse_binary_trace, write_csv


ROOT = Path(__file__).resolve().parents[2]


def frame(trace_format, image):
    version = 5 if trace_format == "ai" else 3
    crc = zlib.crc32(image)
    lines = [f"@TRACE BEGIN format={trace_format} version={version} length={len(image)} crc={crc:08x}"]
    lines.extend(f"@TRACE {offset:08x} {image[offset:offset + 32].hex()}" for offset in range(0, len(image), 32))
    lines.append(f"@TRACE END crc={crc:08x}")
    return "\n".join(lines)


class TraceFrameTest(unittest.TestCase):
    def test_extracts_trace_with_interleaved_logs(self):
        trace_format, image = decode(
            "boot: ready\n@TRACE BEGIN format=ai version=5 length=3 crc=cb5807de\n"
            "@TRACE 00000000 0001ff\nlog between frames\n@TRACE END crc=cb5807de\n"
        )
        self.assertEqual(trace_format, "ai")
        self.assertEqual(image, bytes([0, 1, 255]))

    def test_rejects_missing_or_corrupted_data(self):
        begin = "@TRACE BEGIN format=cpu version=3 length=3 crc=cb5807de\n"
        for body in ["@TRACE 00000001 0001ff\n", "@TRACE 00000000 0001fe\n", "@TRACE 00000000 0001\n"]:
            with self.subTest(body=body), self.assertRaises(ValueError):
                decode(begin + body + "@TRACE END crc=cb5807de\n")

    def test_ai_sample_uart_restoration_has_identical_decoded_json(self):
        sample = ROOT / "host_app/ai_model_monitor/sample/ai_model_monitor.bin"
        raw = sample.read_bytes()
        trace_format, restored = decode(frame("ai", raw))
        self.assertEqual(trace_format, "ai")
        self.assertEqual(restored, raw)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "restored.bin"
            output.write_bytes(restored)
            self.assertEqual(json.dumps(decode_ai(sample), sort_keys=True),
                             json.dumps(decode_ai(output), sort_keys=True))

    def test_cpu_sample_uart_restoration_has_identical_json_and_csv(self):
        sample = ROOT / "host_app/cpu_task_monitor/sample/cpu_task_monitor.bin"
        raw = sample.read_bytes()
        trace_format, restored = decode(frame("cpu", raw))
        self.assertEqual(trace_format, "cpu")
        self.assertEqual(restored, raw)
        original = parse_binary_trace(raw)
        decoded = parse_binary_trace(restored)
        self.assertEqual([asdict(report) for report in decoded], [asdict(report) for report in original])
        with tempfile.TemporaryDirectory() as directory:
            original_csv = Path(directory) / "original.csv"
            restored_csv = Path(directory) / "restored.csv"
            write_csv(original_csv, original)
            write_csv(restored_csv, decoded)
            self.assertEqual(restored_csv.read_bytes(), original_csv.read_bytes())

    def test_compare_cli_writes_only_a_matching_capture(self):
        raw = bytes([0, 1, 255])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            uart = root / "uart.log"
            swd = root / "swd.bin"
            output = root / "restored.bin"
            uart.write_text(frame("ai", raw))
            swd.write_bytes(raw)
            command = [sys.executable, str(Path(__file__).with_name("decode_uart_trace.py")),
                       str(uart), str(output), "--compare", str(swd)]
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), raw)
            swd.write_bytes(bytes([0, 1, 254]))
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("does not match", result.stderr)
            self.assertEqual(output.read_bytes(), raw)


if __name__ == "__main__":
    unittest.main()