"""Host-only tests; no GTest, serial device or firmware build is required."""

import sys
import io
import os
import pty
import termios
import threading
import tempfile
from contextlib import redirect_stderr, redirect_stdout
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tool"))
import verify


def schedule(completed, failed=0, queue=0):
    return (f"cpu: schedule name=submit due={completed} started={completed} completed={completed} "
            f"failed={failed} skipped=0 dropped={queue} late_ms=5 frame_ms=0 idle=2 "
            f"drop_stride=0 drop_inactive=0 drop_source=0 drop_queue={queue}\n")


class RunningApp:
    def __init__(self):
        self.now = 0.0
        self.models = 7
        self.boxes = True
        self.exposure = True
        self.fps = 20
        self.lines = []
        self.commands = []
        self.region = [0, 0, 2592, 1944]
        self.fail_once = None

    def clock(self):
        return self.now

    def wait(self, seconds):
        self.now += seconds

    def command(self, command):
        self.commands.append(command)
        self.now += 0.1
        if command == self.fail_once:
            self.fail_once = None
            raise verify.VerificationError("injected command failure")
        if command.startswith("models "):
            value = command.split()[-1]
            self.models = {"person": 1, "face": 2, "seg": 4, "none": 0, "all": 7}.get(value, None)
            if self.models is None:
                self.models = int(value)
        elif command.startswith("ui boxes "):
            self.boxes = command.endswith("on")
        elif command.startswith("ui exposure "):
            self.exposure = command.endswith("on")
            if not self.exposure:
                self.region = [0, 0, 2592, 1944]
        elif command.startswith("camera fps "):
            self.fps = int(command.split()[-1])
        if command == "camera status":
            frames = int(self.now * 20)
            region = ",".join(map(str, self.region))
            response = (f"camera ae=1 stats={region}\nfps={self.fps} frames={frames}/{frames} errors=0/1/0")
            self.lines.extend(response.splitlines())
            self.lines.append(schedule(int(self.now * 10)).replace("name=submit", "name=results"))
            return response
        return f"models={self.models} boxes={int(self.boxes)} ai_exposure={int(self.exposure)}"


class VerificationTest(unittest.TestCase):
    def test_camera_and_ui_parse_interleaved_logs(self):
        response = ("camera status\ncamera ae=1 exp=9000us gain=1200mdB comp=0 awb=1 temp=5000 "
                    "stats=0,0,2592,1944\nlog: other task\n"
                    "fps=20 flip=0,0 crop=0,0,2592,1944 frames=100/90 errors=0/1/0\n")
        parsed = verify.camera_status(response)
        self.assertEqual(parsed["fps"], 20)
        self.assertEqual(parsed["statistics"], [0, 0, 2592, 1944])
        self.assertEqual(parsed["frames"], [100, 90])
        self.assertEqual(verify.ui_status("models=7 boxes=1 ai_exposure=1"),
                         {"models": 7, "boxes": True, "exposure": True})

    def test_missing_data_is_not_a_pass(self):
        self.assertEqual(verify.analyze("shell ready\n")["status"], "incomplete")
        self.assertEqual(verify.analyze(schedule(1))["status"], "incomplete")
        with self.assertRaises(ValueError):
            verify.camera_status("fps=20")

    def test_schedule_deltas_wrap_and_load_drops_are_warnings(self):
        result = verify.analyze(schedule(0xFFFFFFFF) + schedule(2, queue=3))
        self.assertEqual(result["status"], "observed")
        self.assertEqual(result["operations"]["submit"]["delta"]["completed"], 3)
        self.assertTrue(result["warnings"])

    def test_command_completion_is_not_confused_with_echo_or_other_logs(self):
        class Port:
            def __init__(self):
                self.lines = []
            def write(self, data, timeout):
                self.sent = data
                self.lines = ["camera status", "cpu: period=100 cycles irq=0% count=0 unknown=0",
                              "fps=20 frames=1/1 errors=0/0/0", "uptime", "uptime 42 ms"]
            def readline(self, timeout):
                if not self.lines:
                    raise TimeoutError()
                return self.lines.pop(0)
        port = Port()
        record = io.StringIO()
        session = verify.Session(port, record)
        response = session.command("camera status")
        self.assertIn("fps=20", response)
        self.assertEqual(port.sent, b"camera status\ruptime\r")
        self.assertIn("host: command=camera status", record.getvalue())

    def test_command_error_is_rejected_after_completion(self):
        class Port:
            def __init__(self):
                self.lines = []
            def write(self, data, timeout):
                self.lines = ["error: apply code=2", "failed: camera readback", "uptime 42 ms"]
            def readline(self, timeout):
                if not self.lines:
                    raise TimeoutError()
                return self.lines.pop(0)
        session = verify.Session(Port(), io.StringIO())
        with self.assertRaises(verify.VerificationError):
            session.command("camera fps 20")

    def test_progress_allows_wrap_and_rejects_stopped_pipe(self):
        original = {"frames": [0xFFFFFFFF, 0xFFFFFFFF], "errors": [0, 1, 0]}
        verify.require_progress(original, {"frames": [3, 2], "errors": [0, 1, 0]})
        with self.assertRaises(verify.VerificationError):
            verify.require_progress(original, {"frames": [3, 0xFFFFFFFF], "errors": [0, 1, 0]})

    def test_failure_increase_and_transport_errors_fail(self):
        self.assertEqual(verify.analyze(schedule(1) + schedule(2, failed=1))["status"], "fail")
        self.assertEqual(verify.analyze("error: response timeout\n")["status"], "fail")

    def test_existing_startup_error_is_not_counted_as_a_new_error(self):
        text = ("fps=20 frames=10/10 errors=0/1/0\n" + schedule(1)
                + "fps=20 frames=20/20 errors=0/1/0\n" + schedule(2))
        result = verify.analyze(text)
        self.assertEqual(result["camera_progress"]["errors"], [0, 0, 0])
        self.assertEqual(result["status"], "observed")

    def test_endurance_short_run_is_incomplete_and_restores_settings(self):
        session = RunningApp()
        result = verify.endurance(session, 20.0, 1.0)
        self.assertEqual(result["status"], "incomplete")
        self.assertGreater(result["steps"], 10)
        self.assertEqual((session.models, session.boxes, session.fps), (7, True, 20))

    def test_full_endurance_passes_with_progress(self):
        session = RunningApp()
        result = verify.endurance(session, 1800.0, 10.0)
        self.assertEqual(result["status"], "pass", result["issues"])
        self.assertGreaterEqual(result["seconds"], 1800.0)

    def test_endurance_failure_still_restores_settings(self):
        session = RunningApp()
        session.fail_once = "models face"
        result = verify.endurance(session, 1800.0, 1.0)
        self.assertEqual(result["status"], "fail")
        self.assertEqual((session.models, session.boxes, session.fps), (7, True, 20))

    def test_exposure_requires_a_narrow_region_then_full_return(self):
        session = RunningApp()
        prompts = []
        def prompt(message):
            prompts.append(message)
            session.region = [10, 20, 100, 80] if len(prompts) == 1 else [0, 0, 2592, 1944]
        result = verify.exposure(session, 1.0, prompt)
        self.assertEqual(result["status"], "pass", result["issues"])
        self.assertEqual(result["target"]["statistics"], [10, 20, 100, 80])
        self.assertEqual((session.models, session.exposure), (7, True))

    def test_exposure_without_detection_is_not_a_pass(self):
        session = RunningApp()
        result = verify.exposure(session, 1.0, lambda message: None)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(session.models, 7)

    def test_counter_reset_is_not_mistaken_for_wrap_progress(self):
        with self.assertRaises(verify.VerificationError):
            verify.require_progress({"frames": [100, 100], "errors": [0, 0, 0]},
                                    {"frames": [1, 1], "errors": [0, 0, 0]})

    def test_saved_log_counter_reset_is_a_failure(self):
        self.assertEqual(verify.analyze(schedule(100) + schedule(2))["status"], "fail")

    def test_real_posix_uart_exchange_and_settings_restore_using_pty(self):
        master, slave = pty.openpty()
        original = termios.tcgetattr(slave)
        failures = []
        def device():
            try:
                received = os.read(master, 1024)
                if received != b"ui status\ruptime\r":
                    raise AssertionError(received)
                os.write(master, b"ui status\r\nmodels=7 boxes=1 ai_exposure=1\r\nuptime 42 ms\r\n")
            except BaseException as error:
                failures.append(error)
        try:
            with verify.Uart(os.ttyname(slave), 115200) as port:
                worker = threading.Thread(target=device, daemon=True)
                worker.start()
                response = verify.Session(port, io.StringIO(), timeout=2.0).command("ui status")
                self.assertEqual(verify.ui_status(response)["models"], 7)
                worker.join(timeout=2.0)
                self.assertFalse(worker.is_alive())
                self.assertFalse(failures)
            self.assertEqual(termios.tcgetattr(slave), original)
        finally:
            os.close(master)
            os.close(slave)

    def test_cli_rejects_invalid_duration_before_opening_uart(self):
        for value in ("0", "-1", "nan", "inf"):
            with self.subTest(value=value), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                verify.main(["endurance", "--device", "/missing-device", "--log", "/unused.log", "--seconds", value])
            self.assertEqual(raised.exception.code, 2)

    def test_cli_does_not_overwrite_an_existing_uart_log(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "uart.log"
            log.write_text("existing evidence", encoding="utf-8")
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                verify.main(["endurance", "--device", "/missing-device", "--log", str(log)])
            self.assertEqual(raised.exception.code, 2)
            self.assertEqual(log.read_text(), "existing evidence")

    def test_analyze_cli_writes_json_without_modifying_input(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "uart.log"
            output = Path(directory) / "result.json"
            text = schedule(1) + schedule(2)
            log.write_text(text, encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(verify.main(["analyze", str(log), "--output", str(output)]), 0)
            self.assertEqual(log.read_text(), text)
            self.assertIn('"status": "observed"', output.read_text())
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                verify.main(["analyze", str(log), "--output", str(log)])
            self.assertEqual(log.read_text(), text)


if __name__ == "__main__":
    unittest.main()