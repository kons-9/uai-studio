import argparse
import pathlib
import sys
import time

from manifest import decode, verify

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "experiment-hw-test"))
from uart import Uart


def send(port, command, deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("upload deadline expired")
    port.write((command + "\r").encode("ascii"), remaining)
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("device acknowledgement timeout")
        line = port.readline(remaining).strip().removeprefix("> ")
        if line == "MODEL OK" or (command == "model abort" and line == "MODEL OK abort"):
            return
        if line.startswith("ERR ") or line.startswith("MODEL ERROR"):
            raise ValueError("device rejected command: " + line)


def upload(port, header, weights, blob, timeout):
    manifest = decode(header)
    if not verify(manifest, weights, blob):
        raise ValueError("payload does not match manifest")
    deadline = time.monotonic() + timeout
    send(port, "model begin " + header.hex(), deadline)
    try:
        for name, data in (("weights", weights), ("blob", blob)):
            for offset in range(0, len(data), 32):
                send(port, f"model chunk {name} {offset} {data[offset:offset + 32].hex()}", deadline)
        send(port, "model commit", deadline)
    except (OSError, TimeoutError, ValueError):
        try:
            send(port, "model abort", time.monotonic() + min(timeout, 1))
        except (OSError, TimeoutError, ValueError):
            pass
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uart", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--weights", type=pathlib.Path, required=True)
    parser.add_argument("--blob", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--wait-ready", action="store_true")
    arguments = parser.parse_args()
    if not 0 < arguments.timeout < float("inf"):
        parser.error("timeout must be positive and finite")
    try:
        header = arguments.manifest.read_bytes()
        weights = arguments.weights.read_bytes()
        blob = arguments.blob.read_bytes()
        with Uart(arguments.uart, arguments.baud) as port:
            if arguments.wait_ready:
                deadline = time.monotonic() + arguments.timeout
                while "MODEL READY" not in port.readline(max(0, deadline - time.monotonic())):
                    pass
            upload(port, header, weights, blob, arguments.timeout)
    except (OSError, TimeoutError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
    print("MODEL VERIFIED (NPU not registered)")


if __name__ == "__main__":
    main()