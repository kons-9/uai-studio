import argparse
import dataclasses
import json
import pathlib
import struct
import zlib

PREFIX = struct.Struct("<4sHH10I")
CRC = struct.Struct("<I")
SIZE = PREFIX.size + CRC.size


@dataclasses.dataclass(frozen=True)
class Manifest:
    runtime_version: int
    kind: int
    input_bytes: int
    output_bytes: int
    weights_address: int
    weights_bytes: int
    weights_crc: int
    blob_address: int
    blob_bytes: int
    blob_crc: int

    def validate(self):
        if any(type(value) is not int or not 0 <= value <= 0xffffffff for value in dataclasses.astuple(self)):
            raise ValueError("manifest fields must be uint32")
        if not all((self.runtime_version, self.kind, self.input_bytes, self.output_bytes, self.weights_bytes, self.blob_bytes)):
            raise ValueError("version, kind, tensor sizes and segment sizes must be nonzero")
        segments = [(self.weights_address, self.weights_bytes), (self.blob_address, self.blob_bytes)]
        if any(address + size > 0x100000000 for address, size in segments):
            raise ValueError("segment address overflow")
        if self.weights_address < self.blob_address + self.blob_bytes and self.blob_address < self.weights_address + self.weights_bytes:
            raise ValueError("segments overlap")

    def encode(self):
        self.validate()
        prefix = PREFIX.pack(b"UAIM", 1, SIZE, *dataclasses.astuple(self))
        return prefix + CRC.pack(zlib.crc32(prefix))


def decode(data):
    if len(data) != SIZE:
        raise ValueError("invalid manifest size")
    magic, version, size, *fields = PREFIX.unpack(data[:-4])
    if (magic, version, size) != (b"UAIM", 1, SIZE) or CRC.unpack(data[-4:])[0] != zlib.crc32(data[:-4]):
        raise ValueError("invalid manifest header or checksum")
    manifest = Manifest(*fields)
    manifest.validate()
    return manifest


def verify(manifest, weights, blob):
    manifest.validate()
    return (len(weights), zlib.crc32(weights), len(blob), zlib.crc32(blob)) == (
        manifest.weights_bytes, manifest.weights_crc, manifest.blob_bytes, manifest.blob_crc)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack")
    pack.add_argument("--weights", type=pathlib.Path, required=True)
    pack.add_argument("--blob", type=pathlib.Path, required=True)
    for name in ("runtime-version", "kind", "input-bytes", "output-bytes", "weights-address", "blob-address", "region-base", "region-bytes"):
        pack.add_argument("--" + name, type=lambda value: int(value, 0), required=True)
    pack.add_argument("--output", type=pathlib.Path, required=True)
    check = commands.add_parser("verify")
    check.add_argument("manifest", type=pathlib.Path)
    check.add_argument("--weights", type=pathlib.Path, required=True)
    check.add_argument("--blob", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        weights = arguments.weights.read_bytes()
        blob = arguments.blob.read_bytes()
        if arguments.command == "pack":
            manifest = Manifest(arguments.runtime_version, arguments.kind, arguments.input_bytes, arguments.output_bytes,
                                arguments.weights_address, len(weights), zlib.crc32(weights), arguments.blob_address, len(blob), zlib.crc32(blob))
            manifest.validate()
            end = arguments.region_base + arguments.region_bytes
            if not (0 <= arguments.region_base < end <= 0x100000000):
                raise ValueError("invalid staging region")
            if any(address < arguments.region_base or address + size > end for address, size in
                   ((manifest.weights_address, manifest.weights_bytes), (manifest.blob_address, manifest.blob_bytes))):
                raise ValueError("segment outside reserved region")
            if arguments.output.resolve() in (arguments.weights.resolve(), arguments.blob.resolve()):
                raise ValueError("output must not overwrite model input")
            arguments.output.write_bytes(manifest.encode())
        else:
            manifest = decode(arguments.manifest.read_bytes())
            if not verify(manifest, weights, blob):
                raise ValueError("model payload mismatch")
        print(json.dumps(dataclasses.asdict(manifest), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()