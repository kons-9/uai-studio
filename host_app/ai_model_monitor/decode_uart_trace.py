"""Recover a trace image from framed UART text for the existing trace decoders."""

import argparse
import re
import zlib
from pathlib import Path

BEGIN = re.compile(r"^@TRACE BEGIN format=(ai|cpu) version=(\d+) length=(\d+) crc=([0-9a-f]{8})$")
DATA = re.compile(r"^@TRACE ([0-9a-f]{8}) ((?:[0-9a-f]{2})+)$")
END = re.compile(r"^@TRACE END crc=([0-9a-f]{8})$")
VERSIONS = {"ai": 5, "cpu": 3}


def decode(text: str) -> tuple[str, bytes]:
    trace_format = None
    expected_length = 0
    expected_crc = 0
    image = bytearray()
    for line in text.splitlines():
        line = line.strip()
        match = BEGIN.fullmatch(line)
        if match:
            trace_format, version, length, crc = match.groups()
            if int(version) != VERSIONS[trace_format]:
                raise ValueError("unsupported trace format version")
            expected_length = int(length)
            expected_crc = int(crc, 16)
            image.clear()
            continue
        if trace_format is None:
            continue
        match = DATA.fullmatch(line)
        if match:
            offset, encoded = match.groups()
            if int(offset, 16) != len(image) or len(image) + len(encoded) // 2 > expected_length:
                raise ValueError("missing, duplicate or oversized trace data")
            image.extend(bytes.fromhex(encoded))
            continue
        match = END.fullmatch(line)
        if match:
            if len(image) != expected_length or int(match.group(1), 16) != expected_crc:
                raise ValueError("trace length or checksum trailer mismatch")
            if zlib.crc32(image) != expected_crc:
                raise ValueError("trace checksum mismatch")
            return trace_format, bytes(image)
    raise ValueError("no complete trace frame")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uart_log", type=Path)
    parser.add_argument("output", type=Path, help="raw image for the existing AI/CPU trace decoder")
    parser.add_argument("--compare", type=Path, help="compare against a SWD dump of the same snapshot")
    args = parser.parse_args()
    trace_format, image = decode(args.uart_log.read_text(errors="replace"))
    if args.compare and args.compare.read_bytes() != image:
        parser.error("UART trace does not match SWD dump")
    args.output.write_bytes(image)
    print(f"{trace_format} trace: {len(image)} bytes, CRC32={zlib.crc32(image):08x}")


if __name__ == "__main__":
    main()