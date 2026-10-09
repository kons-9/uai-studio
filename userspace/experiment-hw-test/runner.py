import argparse
import collections
import pathlib
import re
import sys
import time
import xml.etree.ElementTree as ET

from uart import Uart


def parse(lines):
    results = []
    summary = None
    names = set()
    for line in lines:
        line = line.strip()
        if not line.startswith("HWTEST "):
            continue
        totals = re.fullmatch(r"HWTEST SUMMARY pass=(\d+) fail=(\d+) skip=(\d+)", line)
        if totals:
            if summary is not None:
                raise ValueError("duplicate summary")
            summary = tuple(map(int, totals.groups()))
            continue
        result = re.fullmatch(r"HWTEST ([A-Za-z0-9_-]+) (PASS|FAIL|SKIP)(?: (.*))?", line)
        if result is None or summary is not None:
            raise ValueError("malformed or out-of-order test result")
        name, status, detail = result.groups()
        if name in names:
            raise ValueError("duplicate test result: " + name)
        names.add(name)
        results.append((name, status, detail or ""))
    if not results or summary is None:
        raise ValueError("incomplete run: results and SUMMARY required")
    counts = collections.Counter(status for _, status, _ in results)
    if summary != (counts["PASS"], counts["FAIL"], counts["SKIP"]):
        raise ValueError("SUMMARY does not match results")
    return results


def junit(results):
    suite = ET.Element("testsuite", name="experiment-hw-test", tests=str(len(results)),
                       failures=str(sum(status == "FAIL" for _, status, _ in results)),
                       skipped=str(sum(status == "SKIP" for _, status, _ in results)))
    for name, status, detail in results:
        case = ET.SubElement(suite, "testcase", name=name)
        if status != "PASS":
            ET.SubElement(case, "failure" if status == "FAIL" else "skipped", message=detail)
        ET.SubElement(case, "system-out").text = detail
    return ET.tostring(suite, encoding="unicode")


def collect(port, name, timeout, record):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", name):
        raise ValueError("invalid test name")
    command = "hwtest all" if name == "all" else "hwtest run " + name
    port.write((command + "\r").encode("ascii"), timeout)
    deadline = time.monotonic() + timeout
    lines = []
    while True:
        line = port.readline(max(0, deadline - time.monotonic()))
        record.write(line + "\n")
        record.flush()
        if line.strip().startswith("ERR "):
            raise ValueError("device rejected command: " + line)
        lines.append(line)
        if line.strip().startswith("HWTEST SUMMARY "):
            return parse(lines)


def collect_autorun(port, timeout, record):
    deadline = time.monotonic() + timeout
    lines = []
    while True:
        line = port.readline(max(0, deadline - time.monotonic()))
        record.write(line + "\n")
        record.flush()
        if line.strip().startswith("ERR "):
            raise ValueError("device rejected startup run: " + line)
        lines.append(line)
        if line.strip().startswith("HWTEST SUMMARY "):
            return parse(lines)


def live_runs(port, name, repeat, timeout, log_dir, autorun_first=False):
    results = []
    log_dir.mkdir(parents=True, exist_ok=True)
    for iteration in range(1, repeat + 1):
        with (log_dir / f"run-{iteration:03d}.log").open("x") as record:
            try:
                current = (
                    collect_autorun(port, timeout, record)
                    if iteration == 1 and autorun_first
                    else collect(port, name, timeout, record)
                )
            except (OSError, ValueError, TimeoutError) as error:
                current = [("transport", "FAIL", str(error))]
                results.extend((f"run{iteration}-{case}", status, detail) for case, status, detail in current)
                break
        results.extend((f"run{iteration}-{case}", status, detail) for case, status, detail in current)
    return results


def main():
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--log", type=pathlib.Path)
    source.add_argument("--uart")
    parser.add_argument("--junit", type=pathlib.Path, required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--test", default="all")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--log-dir", type=pathlib.Path)
    parser.add_argument(
        "--wait-ready",
        action="store_true",
        help="wait for HWTEST READY and collect the startup all run without sending a shell command",
    )
    arguments = parser.parse_args()
    if arguments.timeout <= 0 or not arguments.timeout < float("inf") or arguments.repeat < 1:
        parser.error("timeout must be positive and finite; repeat must be positive")
    if arguments.uart and arguments.log_dir is None:
        parser.error("--uart requires a new --log-dir")
    if not arguments.uart and (arguments.repeat != 1 or arguments.wait_ready):
        parser.error("repeat and wait-ready require --uart")
    try:
        if arguments.uart:
            with Uart(arguments.uart, arguments.baud) as port:
                if arguments.wait_ready:
                    deadline = time.monotonic() + arguments.timeout
                    while "HWTEST READY" not in port.readline(max(0, deadline - time.monotonic())):
                        pass
                results = live_runs(
                    port,
                    arguments.test,
                    arguments.repeat,
                    arguments.timeout,
                    arguments.log_dir,
                    autorun_first=arguments.wait_ready and arguments.test == "all",
                )
        else:
            text = arguments.log.read_text() if arguments.log else sys.stdin.read()
            results = parse(text.splitlines())
        arguments.junit.write_text(junit(results) + "\n")
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + "\n")
    if any(status == "FAIL" for _, status, _ in results):
        return 1
    return 0 if any(status == "PASS" for _, status, _ in results) else 3


if __name__ == "__main__":
    sys.exit(main())
