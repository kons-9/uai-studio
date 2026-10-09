import argparse
import pathlib
import re
import subprocess
import sys
import time

from manifest import decode, verify
from expected import generate as expected_header
from layout import BLOB_ADDRESS, UPLOAD_CHUNK_BYTES, WEIGHTS_ADDRESS

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
        if line == command or line in ("", ">") or (line and command.startswith(line)):
            continue
        acknowledgements = {"model abort": "MODEL OK abort", "model run": "MODEL OK running"}
        if line == "MODEL OK" or line == acknowledgements.get(command):
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


def synchronize_prompt(port, timeout):
    # HOTPLUG can briefly disturb UART RX while SWD takes control of the core.
    # Discard stale prompt/partial bytes, then send a blank line to clear any
    # receive-error discard state before issuing the adoption command.
    port.flush_input()
    deadline = time.monotonic() + timeout
    port.write(b"\r", max(0.001, deadline - time.monotonic()))
    response = port.read_until(b"> ", max(0.001, deadline - time.monotonic()))
    for line in response.splitlines():
        report(clean_line(line))


def upload(port, header, weights, blob, timeout):
    manifest = decode(header)
    if not verify(manifest, weights, blob):
        raise ValueError("payload does not match manifest")
    deadline = time.monotonic() + timeout
    send(port, "model begin " + header.hex(), deadline)
    try:
        for name, data in (("weights", weights), ("blob", blob)):
            next_progress = 64 * 1024
            for offset in range(0, len(data), UPLOAD_CHUNK_BYTES):
                chunk = data[offset:offset + UPLOAD_CHUNK_BYTES]
                send(port, f"model chunk {name} {offset} {chunk.hex()}", deadline)
                uploaded = offset + len(chunk)
                if uploaded >= next_progress or uploaded == len(data):
                    print(f"UART: uploaded {name} {uploaded}/{len(data)} bytes", flush=True)
                    next_progress = ((uploaded // (64 * 1024)) + 1) * (64 * 1024)
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
    return fields


def load_memory(programmer, serial, weights_path, blob_path, timeout):
    connection = "port=swd mode=HOTPLUG"
    if serial:
        connection += " sn=" + serial
    command = [programmer, "-c", connection,
               "-d", str(weights_path), hex(WEIGHTS_ADDRESS), "-v",
               "-d", str(blob_path), hex(BLOB_ADDRESS), "-v", "-run"]
    print("ST-Link: loading model binaries directly into PSRAM", flush=True)
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        raise TimeoutError("ST-Link direct PSRAM load timed out") from error
    if result.stdout:
        print(result.stdout, end="", flush=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError("ST-Link direct PSRAM load failed" + (": " + detail if detail else ""))


def run_model(port, timeout, expected_crc=None):
    deadline = time.monotonic() + timeout
    try:
        send(port, "model run", deadline)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("model execution timeout")
            line = clean_line(port.readline(remaining))
            report(line)
            if line.startswith("ERR ") or line.startswith("MODEL ERROR"):
                raise ValueError("device execution failed: " + line)
            if not line.startswith("MODEL RESULT "):
                continue
            fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
            if fields.get("npu") != "done" or not re.fullmatch(r"[0-9a-fA-F]{8}", fields.get("output_crc", "")):
                raise ValueError("model execution did not complete: " + line)
            crc = int(fields["output_crc"], 16)
            if expected_crc is not None and crc != expected_crc:
                raise ValueError(f"output CRC mismatch: expected {expected_crc:08x}, got {crc:08x}")
            return crc
    except (OSError, TimeoutError, ValueError):
        try:
            send(port, "model abort", time.monotonic() + min(timeout, 1))
        except (OSError, TimeoutError, ValueError):
            pass
        raise


def read_payload(manifest_path, weights_path, blob_path):
    header = manifest_path.read_bytes()
    weights = weights_path.read_bytes()
    blob = blob_path.read_bytes()
    expected_header(header)
    if not verify(decode(header), weights, blob):
        raise ValueError("payload does not match manifest")
    return header, weights, blob


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uart", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--weights", type=pathlib.Path, required=True)
    parser.add_argument("--blob", type=pathlib.Path, required=True)
    parser.add_argument("--direct-programmer", help="STM32CubeProgrammer CLI; writes binary weights/blob to mapped PSRAM over SWD")
    parser.add_argument("--stlink-serial", help="select an ST-Link probe by serial number")
    parser.add_argument("--timeout", type=float, default=1200,
                        help="whole upload/execution deadline in seconds (default: 1200)")
    parser.add_argument("--startup-timeout", type=float, default=60,
                        help="deadline for firmware and camera startup (default: 60)")
    parser.add_argument("--wait-ready", action="store_true")
    parser.add_argument("--run", action="store_true", help="run the generated model with zero-filled input after upload")
    parser.add_argument("--expected-output-crc", type=lambda value: int(value, 0),
                        help="require this output CRC (for example, the NOR baseline CRC)")
    parser.add_argument("--monitor-after-upload", action="store_true",
                        help="keep the UART open and print device output until Ctrl-C")
    arguments = parser.parse_args()
    if not 0 < arguments.timeout < float("inf") or not 0 < arguments.startup_timeout < float("inf"):
        parser.error("timeouts must be positive and finite")
    if arguments.expected_output_crc is not None and (not arguments.run or not 0 <= arguments.expected_output_crc <= 0xffffffff):
        parser.error("expected-output-crc requires --run and must be a uint32")
    try:
        header, weights, blob = read_payload(arguments.manifest, arguments.weights, arguments.blob)
        with Uart(arguments.uart, arguments.baud) as port:
            if arguments.wait_ready:
                wait_ready(port, arguments.startup_timeout)
            if arguments.direct_programmer:
                send(port, "model abort", time.monotonic() + arguments.timeout)
                load_memory(arguments.direct_programmer, arguments.stlink_serial,
                            arguments.weights, arguments.blob, arguments.timeout)
                synchronize_prompt(port, arguments.startup_timeout)
                send(port, "model adopt " + header.hex(), time.monotonic() + arguments.timeout)
                status = read_status(port, time.monotonic() + arguments.timeout)
                status_fields = dict(re.findall(r"(\w+)=([^ ]+)", status))
                if status_fields.get("state") != "verified" or status_fields.get("weights") != str(len(weights)) \
                        or status_fields.get("blob") != str(len(blob)):
                    raise ValueError("device did not adopt the direct PSRAM payload: " + status)
                print("UART:", status, flush=True)
            else:
                status_fields = upload(port, header, weights, blob, arguments.timeout)
            if arguments.run:
                if status_fields.get("npu") == "unavailable":
                    raise ValueError("firmware has no NPU backend; build with EXPERIMENT_MODEL_NPU=ON")
                run_model(port, arguments.timeout, arguments.expected_output_crc)
            if arguments.monitor_after_upload:
                print("MODEL VERIFIED on device; monitoring UART, Ctrl-C to stop.", flush=True)
                while True:
                    try:
                        report(clean_line(port.readline(1.0)))
                    except TimeoutError:
                        continue
    except (OSError, TimeoutError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
    print("MODEL VERIFIED on device")


if __name__ == "__main__":
    main()
