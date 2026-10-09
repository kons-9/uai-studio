import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import zlib

from manifest import decode, verify
from contract import verify_package
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


def send_header(port, header, command, deadline):
    decode(header)
    if header[4] == 1:
        send(port, command + " " + header.hex(), deadline)
        return
    for offset in range(0, len(header), 64):
        send(port, f"model header {offset} {header[offset:offset + 64].hex()}", deadline)
    send(port, command, deadline)


def upload(port, header, weights, blob, timeout):
    manifest = decode(header)
    if not verify(manifest, weights, blob):
        raise ValueError("payload does not match manifest")
    deadline = time.monotonic() + timeout
    try:
        send_header(port, header, "model begin", deadline)
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


def load_memory(programmer, serial, weights_path, blob_path, timeout, input_path=None):
    connection = "port=swd mode=HOTPLUG"
    if serial:
        connection += " sn=" + serial
    command = [programmer, "-c", connection,
               "-d", str(weights_path), hex(WEIGHTS_ADDRESS), "-v",
               "-d", str(blob_path), hex(BLOB_ADDRESS), "-v"]
    if input_path:
        command += ["-d", str(input_path), "0x91000000", "-v"]
    command += ["-run"]
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


def run_model(port, timeout, expected_crc=None, kind=None):
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
            if kind is not None and fields.get("kind") != str(kind):
                raise ValueError("result model kind mismatch")
            if expected_crc is not None and crc != expected_crc:
                raise ValueError(f"output CRC mismatch: expected {expected_crc:08x}, got {crc:08x}")
            return crc
    except (OSError, TimeoutError, ValueError):
        try:
            send(port, "model abort", time.monotonic() + min(timeout, 1))
        except (OSError, TimeoutError, ValueError):
            pass
        raise


def upload_input(port, data, timeout, direct=False):
    deadline = time.monotonic() + timeout
    crc = zlib.crc32(data)
    send(port, f"model input {'adopt' if direct else 'begin'} {len(data)} {crc:08x}", deadline)
    if direct:
        return
    for offset in range(0, len(data), UPLOAD_CHUNK_BYTES):
        send(port, f"model input chunk {offset} {data[offset:offset + UPLOAD_CHUNK_BYTES].hex()}", deadline)
    send(port, "model input commit", deadline)


def read_result(port, bytes_count, expected_crc, timeout):
    deadline = time.monotonic() + timeout
    result = bytearray()
    while len(result) < bytes_count:
        offset = len(result)
        count = min(64, bytes_count - offset)
        command = f"model result {offset} {count}"
        port.write((command + "\r").encode("ascii"), max(0.001, deadline - time.monotonic()))
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("result transfer timeout")
            line = clean_line(port.readline(remaining))
            if line.startswith("ERR ") or line.startswith("MODEL ERROR"):
                raise ValueError("result transfer failed: " + line)
            if line.startswith("MODEL DATA "):
                fields = line.split()
                if len(fields) != 4 or fields[2] != str(offset):
                    raise ValueError("unexpected result offset")
                chunk = bytes.fromhex(fields[3])
                if len(chunk) != count:
                    raise ValueError("unexpected result size")
                result.extend(chunk)
                break
    if zlib.crc32(result) != expected_crc:
        raise ValueError("result payload CRC mismatch")
    return bytes(result)


def read_payload(manifest_path, weights_path, blob_path):
    header = manifest_path.read_bytes()
    weights = weights_path.read_bytes()
    blob = blob_path.read_bytes()
    expected_header(header)
    if not verify(decode(header), weights, blob):
        raise ValueError("payload does not match manifest")
    verify_package(manifest_path, weights_path, blob_path)
    return header, weights, blob


def main(argv=None):
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
    parser.add_argument("--run", action="store_true", help="run the loaded model after upload")
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument("--input", type=pathlib.Path, help="prepared input tensor binary")
    inputs.add_argument("--image", type=pathlib.Path, help="image to preprocess according to the v2 Header")
    inputs.add_argument("--input-zeros", action="store_true", help="explicit zero-filled tensor for a smoke test")
    parser.add_argument("--result-dir", type=pathlib.Path, help="save raw outputs, Header, JSON and NumPy tensors")
    parser.add_argument("--policy", type=pathlib.Path, help="separate versioned result decision policy")
    parser.add_argument("--expected-output-crc", type=lambda value: int(value, 0),
                        help="require this output CRC (for example, the NOR baseline CRC)")
    parser.add_argument("--monitor-after-upload", action="store_true",
                        help="keep the UART open and print device output until Ctrl-C")
    arguments = parser.parse_args(argv)
    if not 0 < arguments.timeout < float("inf") or not 0 < arguments.startup_timeout < float("inf"):
        parser.error("timeouts must be positive and finite")
    if arguments.expected_output_crc is not None and (not arguments.run or not 0 <= arguments.expected_output_crc <= 0xffffffff):
        parser.error("expected-output-crc requires --run and must be a uint32")
    if arguments.result_dir and not arguments.run:
        parser.error("result-dir requires --run")
    if arguments.policy and not arguments.run:
        parser.error("policy requires --run")
    try:
        header, weights, blob = read_payload(arguments.manifest, arguments.weights, arguments.blob)
        model = decode(header)
        document = verify_package(arguments.manifest, arguments.weights, arguments.blob)
        policy = None
        if document is not None:
            from results import validate_decoder
            validate_decoder(document)
        if arguments.policy:
            from results import validate_policy
            if document is None:
                raise ValueError("policy requires a v3 semantic contract")
            policy = validate_policy(json.loads(arguments.policy.read_text()))
        if arguments.result_dir and model.input is None:
            raise ValueError("result-dir requires a v2 output descriptor")
        input_data = None
        image_metadata = None
        if arguments.input or arguments.image or arguments.input_zeros:
            if model.input is None:
                raise ValueError("image/input transfer requires a v2 model Header")
            if arguments.image:
                from tensor_io import prepare_image
                input_data, image_metadata = prepare_image(arguments.image, model)
            else:
                input_data = arguments.input.read_bytes() if arguments.input else bytes(model.input_bytes)
            if len(input_data) != model.input_bytes:
                raise ValueError("input tensor size does not match Header")
        if arguments.run and model.input is not None and input_data is None:
            raise ValueError("v2 inference requires --image, --input or explicit --input-zeros")
        with tempfile.TemporaryDirectory(prefix="model-input-") as temporary:
            input_path = pathlib.Path(temporary) / "input.bin"
            if input_data is not None:
                input_path.write_bytes(input_data)
            execute(arguments, header, weights, blob, model, input_data, input_path, image_metadata, document, policy)
    except (OSError, TimeoutError, ValueError, ImportError) as error:
        parser.exit(1, str(error) + "\n")
    print("MODEL VERIFIED on device")


def execute(arguments, header, weights, blob, model, input_data, input_path, image_metadata=None, document=None, policy=None):
        with Uart(arguments.uart, arguments.baud) as port:
            if arguments.wait_ready:
                wait_ready(port, arguments.startup_timeout)
            if arguments.direct_programmer:
                send(port, "model abort", time.monotonic() + arguments.timeout)
                load_memory(arguments.direct_programmer, arguments.stlink_serial,
                            arguments.weights, arguments.blob, arguments.timeout, input_path if input_data is not None else None)
                synchronize_prompt(port, arguments.startup_timeout)
                send_header(port, header, "model adopt", time.monotonic() + arguments.timeout)
                status = read_status(port, time.monotonic() + arguments.timeout)
                status_fields = dict(re.findall(r"(\w+)=([^ ]+)", status))
                if status_fields.get("state") != "verified" or status_fields.get("weights") != str(len(weights)) \
                        or status_fields.get("blob") != str(len(blob)):
                    raise ValueError("device did not adopt the direct PSRAM payload: " + status)
                print("UART:", status, flush=True)
            else:
                status_fields = upload(port, header, weights, blob, arguments.timeout)
            if input_data is not None:
                upload_input(port, input_data, arguments.timeout, bool(arguments.direct_programmer))
            if arguments.run:
                if status_fields.get("npu") == "unavailable":
                    raise ValueError("firmware has no NPU backend; build with MODEL_LOADER_NPU=ON")
                crc = run_model(port, arguments.timeout, arguments.expected_output_crc, model.kind if model.input else None)
                if model.input:
                    from tensor_io import save_result
                    data = read_result(port, model.output_bytes, crc, arguments.timeout)
                    destination = arguments.result_dir or arguments.manifest.parent / "results"
                    save_result(destination, header, model, data, crc, image_metadata, document, policy)
                    print("RESULT saved:", destination, flush=True)
            if arguments.monitor_after_upload:
                print("MODEL VERIFIED on device; monitoring UART, Ctrl-C to stop.", flush=True)
                while True:
                    try:
                        report(clean_line(port.readline(1.0)))
                    except TimeoutError:
                        continue


if __name__ == "__main__":
    main()
