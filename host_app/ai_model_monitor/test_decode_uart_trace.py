import unittest

from decode_uart_trace import decode


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


if __name__ == "__main__":
    unittest.main()