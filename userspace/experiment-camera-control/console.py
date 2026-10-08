import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "experiment-hw-test"))
from uart import Uart


def execute(port, command, timeout):
    if not command or "\r" in command or "\n" in command or len(command) >= 128:
        raise ValueError("command must fit one shell line")
    deadline = time.monotonic() + timeout
    port.write((command + "\r").encode("ascii"), timeout)
    response = port.read_until(b"> ", max(0, deadline - time.monotonic()))
    if any(line.startswith("ERR ") for line in response.splitlines()):
        raise ValueError(response.strip())
    return response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uart", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--wait-ready", action="store_true")
    parser.add_argument("--command", action="append", default=[])
    options = parser.parse_args()
    if not 0 < options.timeout < float("inf"):
        parser.error("timeout must be positive and finite")
    try:
        with Uart(options.uart, options.baud) as port:
            if options.wait_ready:
                print(port.read_until(b"> ", options.timeout), end="", flush=True)
            commands = options.command if options.command else (line.strip() for line in sys.stdin)
            for command in commands:
                if command:
                    print(execute(port, command, options.timeout), end="", flush=True)
    except (OSError, ValueError, TimeoutError, UnicodeError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()