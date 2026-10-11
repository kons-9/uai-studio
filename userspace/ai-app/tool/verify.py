"""Verify the running ai-app with its existing UART shell or a saved log."""

import argparse
import fcntl
import json
import math
import os
import re
import select
import stat
import sys
import termios
import time
import tty
from dataclasses import asdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from host_app.cpu_task_monitor.cpu_task_monitor import parse_reports


CAMERA = re.compile(r"camera ae=(\d+).*?stats=(\d+),(\d+),(\d+),(\d+)")
FRAMES = re.compile(r"fps=(\d+).*?frames=(\d+)/(\d+) errors=(\d+)/(\d+)/(\d+)")
UI = re.compile(r"models=(\d+) boxes=(\d+) ai_exposure=(\d+)")
FATAL = re.compile(r"(?:^|\s)(?:error:|failed:)|lease.*(?:error|failed|invalid)|"
                   r"ai:.*failed|camera:.*(?:process failed|wake.*failed)|previous command: error code=", re.IGNORECASE)
UPTIME = re.compile(r"^uptime \d+ ms$")


class VerificationError(Exception):
    pass


class NotObserved(VerificationError):
    pass


def resolve_device(device):
    if device != "auto":
        return device
    matches = []
    serial = os.environ.get("STM32_PROGRAM_SERIAL", "")
    for candidate in sorted(Path("/dev").glob("ttyACM*")):
        node = (Path("/sys/class/tty") / candidate.name / "device").resolve()
        for parent in (node, *node.parents):
            if not (parent / "idVendor").is_file():
                continue
            if ((parent / "idVendor").read_text().strip() == "0483"
                    and (parent / "idProduct").read_text().strip() == "3754"
                    and (not serial or (parent / "serial").read_text().strip() == serial)):
                matches.append(str(candidate))
            break
    if len(matches) != 1:
        raise ValueError("one ST-LINK VCP is required; specify --device explicitly")
    return matches[0]


class Uart:
    def __init__(self, device, baud):
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise ValueError("unsupported UART baud rate")
        device_stat = os.stat(device)
        if not stat.S_ISCHR(device_stat.st_mode):
            raise ValueError("UART device must be a character device")
        for process in Path("/proc").iterdir():
            if not process.name.isdigit() or int(process.name) == os.getpid():
                continue
            try:
                descriptors = list((process / "fd").iterdir())
            except OSError:
                continue
            for descriptor in descriptors:
                try:
                    opened = descriptor.stat()
                except OSError:
                    continue
                if stat.S_ISCHR(opened.st_mode) and opened.st_rdev == device_stat.st_rdev:
                    raise ValueError(f"UART already open by process {process.name}; close make monitor first")
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.original = None
        self.exclusive = False
        self.close_errors = []
        self.buffer = bytearray()
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.exclusive = True
            self.original = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[2] |= termios.CLOCAL | termios.CREAD
            settings[2] &= ~getattr(termios, "CRTSCTS", 0)
            settings[4] = settings[5] = speed
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.fd is None:
            return
        try:
            if self.original is not None:
                termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
        except OSError as error:
            self.close_errors.append(str(error))
        finally:
            try:
                if self.exclusive:
                    fcntl.ioctl(self.fd, termios.TIOCNXCL)
            except OSError as error:
                self.close_errors.append(str(error))
            finally:
                os.close(self.fd)
                self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *arguments):
        self.close()

    def write(self, data, timeout):
        deadline = time.monotonic() + timeout
        for value in data:
            pending = memoryview(bytes((value,)))
            while pending:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                    raise TimeoutError("UART write timed out")
                try:
                    count = os.write(self.fd, pending)
                except BlockingIOError:
                    continue
                if count == 0:
                    raise ConnectionError("UART disconnected")
                pending = pending[count:]
            time.sleep(0.002)

    def readline(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self.buffer[:newline]).rstrip(b"\r")
                del self.buffer[:newline + 1]
                return line.decode("ascii", errors="replace")
            if len(self.buffer) >= 4096:
                raise ValueError("UART line exceeds 4096 bytes")
            remaining = deadline - time.monotonic()
            if not select.select([self.fd], [], [], max(0.0, remaining))[0]:
                raise TimeoutError("UART read timed out")
            try:
                data = os.read(self.fd, 4096 - len(self.buffer))
            except BlockingIOError:
                continue
            if not data:
                raise ConnectionError("UART disconnected")
            self.buffer.extend(data)


class Session:
    def __init__(self, port, record, timeout=5.0, clock=time.monotonic):
        self.port = port
        self.record = record
        self.timeout = timeout
        self.clock = clock
        self.lines = []
        self.commands = []

    def read(self, timeout):
        try:
            line = self.port.readline(timeout)
        except TimeoutError:
            return None
        self.lines.append(line)
        self.record.write(line + "\n")
        self.record.flush()
        return line

    def wait(self, seconds):
        deadline = self.clock() + seconds
        while self.clock() < deadline:
            self.read(deadline - self.clock())

    @staticmethod
    def response_complete(command, lines):
        response = "\n".join(lines)
        if any(FATAL.search(line) for line in lines):
            return True
        if command.startswith(("models ", "ui ")):
            return UI.search(response) is not None
        if command == "camera" or command == "camera status":
            return CAMERA.search(response) is not None and FRAMES.search(response) is not None
        if command.startswith("camera "):
            return re.search(r"(?:^|\n)ok ae=", response) is not None
        if command.startswith("log "):
            return re.search(r"(?:^|\n)log (?:error|warn|info|debug|trace)$", response) is not None
        return False

    def command(self, command):
        for _ in range(512):
            if self.read(0.0) is None:
                break
        self.commands.append(command)
        self.record.write(f"host: command={command}\n")
        self.record.flush()
        self.port.write((command + "\r").encode("ascii"), self.timeout)
        deadline = self.clock() + self.timeout
        lines = []
        while self.clock() < deadline:
            line = self.read(deadline - self.clock())
            if line is None:
                break
            lines.append(line)
            if self.response_complete(command, lines):
                break
        else:
            raise TimeoutError(f"no command response: {command}; request may still apply")
        if not lines or not self.response_complete(command, lines):
            raise TimeoutError(f"no command response: {command}; request may still apply")
        self.port.write(b"uptime\r", self.timeout)
        deadline = self.clock() + self.timeout
        while self.clock() < deadline:
            line = self.read(deadline - self.clock())
            if line is None:
                break
            lines.append(line)
            if UPTIME.fullmatch(line.strip()):
                response = "\n".join(lines)
                if any(FATAL.search(value) for value in lines):
                    raise VerificationError(f"command rejected or failed: {command}")
                return response
        raise TimeoutError(f"no command completion: {command}; request may still apply")


def require_progress(previous, current):
    if any(delta(current["frames"][index], previous["frames"][index]) > 0x7FFFFFFF for index in range(2)):
        raise VerificationError("camera counters reset; possible reboot")
    if any(delta(current["errors"][index], previous["errors"][index]) for index in range(3)):
        raise VerificationError("camera error counters increased")
    if not all(delta(current["frames"][index], previous["frames"][index]) for index in range(2)):
        raise VerificationError("one or both camera frame counters stopped")


def restore(session, original_ui, original_camera):
    failures = []
    commands = [f"models {original_ui['models']}", f"ui boxes {'on' if original_ui['boxes'] else 'off'}",
                f"camera fps {original_camera['fps']}", f"ui exposure {'on' if original_ui['exposure'] else 'off'}"]
    for command in commands:
        try:
            session.command(command)
        except (OSError, ValueError, TimeoutError, VerificationError) as error:
            failures.append(str(error))
    try:
        current_ui = ui_status(session.command("ui status"))
        current_camera = camera_status(session.command("camera status"))
        if (current_ui != original_ui or current_camera["fps"] != original_camera["fps"]
            or current_camera["ae"] != original_camera["ae"]):
            failures.append("restored settings did not match initial readback")
    except (OSError, ValueError, TimeoutError, VerificationError) as error:
        failures.append(str(error))
    return failures


def endurance(session, seconds, step_seconds):
    original_ui = ui_status(session.command("ui status"))
    original_camera = camera_status(session.command("camera status"))
    started = session.clock()
    previous = original_camera
    steps = 0
    issues = []
    sequence = ["models person", "models face", "models seg", "models none", "models all",
                "ui boxes off", "ui boxes on", "camera fps 10", "camera fps 30",
                f"camera fps {original_camera['fps']}"]
    masks = {"person": 1, "face": 2, "seg": 4, "none": 0, "all": 7}
    try:
        while session.clock() - started < seconds:
            command = sequence[steps % len(sequence)]
            response = session.command(command)
            if command.startswith("models "):
                if ui_status(response)["models"] != masks[command.split()[1]]:
                    raise VerificationError("model readback did not match request")
            elif command.startswith("ui boxes "):
                if ui_status(response)["boxes"] != command.endswith("on"):
                    raise VerificationError("boxes readback did not match request")
            session.wait(min(step_seconds, max(0.0, seconds - (session.clock() - started))))
            current = camera_status(session.command("camera status"))
            require_progress(previous, current)
            if command.startswith("camera fps ") and current["fps"] != int(command.split()[-1]):
                raise VerificationError("FPS readback did not match request")
            previous = current
            steps += 1
    except (OSError, ValueError, TimeoutError, VerificationError, KeyboardInterrupt) as error:
        issues.append(str(error) or "interrupted")
    finally:
        elapsed = session.clock() - started
        issues.extend(restore(session, original_ui, original_camera))
    analysis = analyze("\n".join(session.lines))
    issues.extend(analysis["issues"])
    results = analysis["operations"].get("results", {})
    result_progress = results.get("delta", {}).get("completed", 0)
    if results and not result_progress:
        issues.append("inference-result counter did not advance")
    if not results:
        analysis["warnings"].append("no results schedule samples; enable info-level logging before retrying")
    status = "fail" if issues else "pass" if elapsed >= 1800.0 and result_progress and analysis["status"] == "observed" else "incomplete"
    return {"check": "endurance", "status": status, "seconds": elapsed, "steps": steps,
            "issues": issues, "initial_ui": original_ui, "initial_camera": original_camera,
            "analysis": analysis, "commands": session.commands}


def wait_statistics(session, baseline, timeout, narrowed):
    deadline = session.clock() + timeout
    while session.clock() < deadline:
        current = camera_status(session.command("camera status"))
        if (current["statistics"] != baseline) == narrowed:
            return current
        session.wait(min(0.25, max(0.0, deadline - session.clock())))
    if narrowed:
        raise NotObserved("no narrowed target region; full-frame return was not tested")
    raise VerificationError("statistics did not return to full frame")


def exposure(session, timeout, prompt=input):
    original_ui = ui_status(session.command("ui status"))
    original_camera = camera_status(session.command("camera status"))
    if not original_camera["ae"] or not original_ui["exposure"]:
        raise VerificationError("start with automatic AE and AI exposure ON; manual settings are not modified")
    issues = []
    baseline = None
    target = None
    returned = None
    return_seconds = None
    incomplete = False
    try:
        session.command("models none")
        session.command("ui exposure off")
        session.wait(0.75)
        baseline = camera_status(session.command("camera status"))["statistics"]
        if baseline[:2] != [0, 0] or not all(value > 0 for value in baseline[2:]):
            raise VerificationError("could not establish full-frame statistics baseline")
        session.command("ui exposure on")
        session.command("models face")
        prompt("Place a detectable face in the camera input, then press Enter: ")
        target = wait_statistics(session, baseline, timeout, narrowed=True)
        prompt("Remove the target from the camera input, then press Enter: ")
        started = session.clock()
        returned = wait_statistics(session, baseline, timeout, narrowed=False)
        return_seconds = session.clock() - started
        session.wait(0.5)
        confirmed = camera_status(session.command("camera status"))
        if confirmed["statistics"] != baseline or not confirmed["ae"]:
            raise VerificationError("full-frame return was not stable or AE stopped")
        require_progress(original_camera, confirmed)
    except NotObserved as error:
        issues.append(str(error))
        incomplete = True
    except (OSError, ValueError, TimeoutError, VerificationError, KeyboardInterrupt) as error:
        issues.append(str(error) or "interrupted")
    finally:
        restore_failures = restore(session, original_ui, original_camera)
        issues.extend(restore_failures)
        if restore_failures:
            incomplete = False
    return {"check": "exposure", "status": "incomplete" if incomplete else "fail" if issues else "pass", "issues": issues,
            "full_frame": baseline, "target": target, "returned": returned,
            "return_seconds_after_enter": return_seconds, "commands": session.commands,
            "limits": ["Manual target movement prevents an exact 1-second hold/250-ms cadence measurement."]}


def delta(current, previous):
    return (current - previous) & 0xFFFFFFFF


def camera_status(text):
    controls = CAMERA.search(text)
    counters = FRAMES.search(text)
    if controls is None or counters is None:
        raise ValueError("incomplete camera status response")
    values = list(map(int, controls.groups()))
    count_values = list(map(int, counters.groups()))
    return {"ae": bool(values[0]), "statistics": values[1:], "fps": count_values[0],
            "frames": count_values[1:3], "errors": count_values[3:]}


def ui_status(text):
    match = UI.search(text)
    if match is None:
        raise ValueError("incomplete UI status response")
    models, boxes, exposure = map(int, match.groups())
    return {"models": models, "boxes": bool(boxes), "exposure": bool(exposure)}


def analyze(text):
    histories = {}
    for report in parse_reports(text):
        for sample in report.schedules:
            histories.setdefault(sample.name, []).append(asdict(sample))
    operations = {}
    issues = [line for line in text.splitlines() if FATAL.search(line)]
    warnings = []
    for name, samples in histories.items():
        keys = (
            "due", "started", "completed", "failed", "skipped", "dropped", "idle_wakeups",
            "dropped_stride", "dropped_inactive", "dropped_source", "dropped_queue")
        increments = [{key: delta(current[key], previous[key]) for key in keys}
                      for previous, current in zip(samples, samples[1:])]
        if any(value > 0x7FFFFFFF for increment in increments for value in increment.values()):
            issues.append(f"{name}: cumulative counters reset or are out of order")
        counts = {key: sum(increment[key] for increment in increments) for key in keys}
        operations[name] = {"samples": len(samples), "delta": counts,
                            "max_lateness_ms": max(sample["max_lateness_ms"] for sample in samples),
                            "max_frame_delay_ms": max(sample["max_frame_delay_ms"] for sample in samples)}
        if counts["failed"]:
            message = f"{name}: failed increased by {counts['failed']}"
            if name == "display":
                warnings.append(
                    message + "; display handoff can count routine buffer backpressure here, and visual checks are out of scope"
                )
            else:
                issues.append(message)
        if counts["dropped_source"]:
            issues.append(f"{name}: input preparation drops increased by {counts['dropped_source']}")
        if counts["dropped_queue"]:
            warnings.append(f"{name}: queue drops increased by {counts['dropped_queue']}; correlate with load")
    counters = [list(map(int, match.groups())) for match in FRAMES.finditer(text)]
    progress = None
    if len(counters) >= 2:
        first, last = counters[0], counters[-1]
        progress = {"frames": [delta(last[index], first[index]) for index in (1, 2)],
                    "errors": [delta(last[index], first[index]) for index in (3, 4, 5)]}
        if any(delta(current[index], previous[index]) > 0x7FFFFFFF
               for previous, current in zip(counters, counters[1:]) for index in range(1, 6)):
            issues.append("camera counters reset or are out of order")
        if any(progress["errors"]):
            issues.append("camera error counters increased")
        if not all(progress["frames"]):
            issues.append("one or both camera frame counters did not advance")
    enough = bool(operations) and all(len(samples) >= 2 for samples in histories.values())
    return {"status": "fail" if issues else "observed" if enough else "incomplete",
            "operations": operations, "camera_progress": progress, "issues": issues,
            "warnings": warnings,
            "limits": ["No previous firmware comparison or performance improvement claim.",
                       "Cumulative maximum delays do not prove latency recovered after load.",
                       "No fault injection, frame-stop recovery, visual or touch verification."]}


def write_result(result, output):
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    log = commands.add_parser("analyze", help="summarize current-firmware UART statistics")
    log.add_argument("log", type=Path)
    log.add_argument("--output", type=Path)
    for name, help_text in [("endurance", "run shell operations for 30 minutes"),
                            ("exposure", "verify target-region to full-frame return")]:
        live = commands.add_parser(name, help=help_text)
        live.add_argument("--device", default="auto")
        live.add_argument("--baud", type=int, default=115200)
        live.add_argument("--log", type=Path, required=True, help="new UART log file; never overwritten")
        live.add_argument("--output", type=Path)
        live.add_argument("--timeout", type=float, default=5.0, help="command response deadline in seconds")
        if name == "endurance":
            live.add_argument("--seconds", type=float, default=1800.0)
            live.add_argument("--step-seconds", type=float, default=10.0)
        else:
            live.add_argument("--observe-timeout", type=float, default=10.0)
    args = parser.parse_args(argv)
    for key in ("timeout", "seconds", "step_seconds", "observe_timeout"):
        value = getattr(args, key, None)
        if value is not None and (not math.isfinite(value) or value <= 0):
            parser.error(f"--{key.replace('_', '-')} must be positive and finite")
    if args.output is not None:
        inputs = [args.log]
        if args.output.resolve() in [path.resolve() for path in inputs]:
            parser.error("output must not overwrite the UART log")
    try:
        if args.command == "analyze":
            result = analyze(args.log.read_text(encoding="utf-8", errors="replace"))
        else:
            device = resolve_device(args.device)
            args.log.parent.mkdir(parents=True, exist_ok=True)
            with args.log.open("x", encoding="utf-8") as record, Uart(device, args.baud) as port:
                session = Session(port, record, args.timeout)
                result = endurance(session, args.seconds, args.step_seconds) if args.command == "endurance" else exposure(
                    session, args.observe_timeout)
                result["device"] = device
            if port.close_errors:
                result["status"] = "fail"
                result["issues"].extend(f"UART cleanup: {error}" for error in port.close_errors)
        write_result(result, args.output)
        return 1 if result["status"] == "fail" else 3 if result["status"] == "incomplete" else 0
    except (OSError, ValueError, TimeoutError, VerificationError) as error:
        parser.exit(2, f"verify: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
