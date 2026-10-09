#!/usr/bin/env python3
"""Create a tiny deterministic payload for exercising the PSRAM transfer path."""

import argparse
import pathlib
import zlib

from manifest import Manifest
from layout import BLOB_ADDRESS, WEIGHTS_ADDRESS


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def create(output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    weights = b"UAI model-load transfer fixture: weights v1\n" + bytes(range(32))
    blob = b"UAI model-load transfer fixture: blob v1\n"
    weights_path = output_dir / "weights.bin"
    blob_path = output_dir / "blob.bin"
    manifest_path = output_dir / "manifest.bin"
    write_if_changed(weights_path, weights)
    write_if_changed(blob_path, blob)
    write_if_changed(
        manifest_path,
        Manifest(
            runtime_version=1,
            kind=1,
            input_bytes=1,
            output_bytes=1,
            weights_address=WEIGHTS_ADDRESS,
            weights_bytes=len(weights),
            weights_crc=zlib.crc32(weights),
            blob_address=BLOB_ADDRESS,
            blob_bytes=len(blob),
            blob_crc=zlib.crc32(blob),
        ).encode(),
    )
    return manifest_path, weights_path, blob_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    manifest_path, weights_path, blob_path = create(args.output_dir)
    print(f"manifest={manifest_path}")
    print(f"weights={weights_path}")
    print(f"blob={blob_path}")


if __name__ == "__main__":
    main()
