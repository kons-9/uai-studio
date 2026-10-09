import argparse
import pathlib
import re
import sys
import time

from manifest import decode, verify

from uart import Uart


def clean_line(line):
    line = line.strip()
    if line.startswith("> "):
        line = line[2:].strip()
    return line


def report(line):
    if line:
        print("UART:", line, flush=True)


def wait_ready(port, timeout):
    deadline = time.monotonic() + timeout
    model_ready = False
    camera_started = False
    while not (model_ready and camera_started):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("timed out waiting for model and camera startup")
        line = clean_line(port.readline(remaining))
        report(line)
        if line.startswith("MODEL ERROR"):
            raise ValueError("device startup failed: " + line)
        model_ready = model_ready or "MODEL READY" in line
        camera_started = camera_started or "camera: pipe1=started pipe2=started" in line


def send(port, command, deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("upload deadline expired")
    port.write((command + "\r").encode("ascii"), remaining)
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("device acknowledgement timeout")
        line = clean_line(port.readline(remaining))
        if line == command or line in ("", ">"):
            continue
        if line == "MODEL OK" or (command == "model abort" and line == "MODEL OK abort"):
            return
        if line.startswith("ERR ") or line.startswith("MODEL ERROR"):
            raise ValueError("device rejected command: " + line)
        report(line)


def read_status(port, deadline):
    command = "model stat"
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("device status deadline expired")
    port.write((command + "\r").encode("ascii"), remaining)
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("device status timeout")
        line = clean_line(port.readline(remaining))
        if line == command or line in ("", ">"):
            continue
        if line.startswith("ERR ") or line.startswith("MODEL ERROR"):
            raise ValueError("device status failed: " + line)
        if line.startswith("MODEL state="):
            return line
        if line == "MODEL OK":
            continue
        report(line)


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
    status = read_status(port, deadline)
    fields = dict(re.findall(r"(\w+)=([^ ]+)", status))
    if fields.get("state") != "verified" or fields.get("weights") != str(len(weights)) or fields.get("blob") != str(len(blob)):
        raise ValueError("device did not verify the uploaded payload: " + status)
    print("UART:", status, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uart", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--weights", type=pathlib.Path, required=True)
    parser.add_argument("--blob", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--wait-ready", action="store_true")
    parser.add_argument("--monitor-after-upload", action="store_true",
                        help="keep the UART open and print device output until Ctrl-C")
    arguments = parser.parse_args()
    if not 0 < arguments.timeout < float("inf"):
        parser.error("timeout must be positive and finite")
    try:
        header = arguments.manifest.read_bytes()
        weights = arguments.weights.read_bytes()
        blob = arguments.blob.read_bytes()
        with Uart(arguments.uart, arguments.baud) as port:
            if arguments.wait_ready:
                wait_ready(port, arguments.timeout)
            upload(port, header, weights, blob, arguments.timeout)
            if arguments.monitor_after_upload:
                print("MODEL VERIFIED on device (NPU not registered); monitoring UART, Ctrl-C to stop.", flush=True)
                while True:
                    try:
                        report(clean_line(port.readline(1.0)))
                    except TimeoutError:
                        continue
    except (OSError, TimeoutError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
    print("MODEL VERIFIED on device (NPU not registered)")


if __name__ == "__main__":
    main()
