import argparse
import dataclasses
import json
import math
import pathlib
import struct
import zlib

PREFIX = struct.Struct("<4sHH10I")
CRC = struct.Struct("<I")
SIZE = PREFIX.size + CRC.size
FIELDS = ("runtime_version", "kind", "input_bytes", "output_bytes", "weights_address", "weights_bytes", "weights_crc",
          "blob_address", "blob_bytes", "blob_crc")
TENSOR = struct.Struct("<4B5Ifi2I")
TYPES = {"uint8": 1, "int8": 2, "float32": 3, "int16": 4, "uint16": 5, "float16": 6, "int32": 7}
LAYOUTS = {"raw": 0, "nhwc": 1, "nchw": 2}


@dataclasses.dataclass(frozen=True)
class Tensor:
    type: int
    layout: int
    shape: tuple
    bytes: int
    scale: float = 1.0
    zero: int = 0
    offset: int = 0
    role: int = 0

    def validate(self):
        widths = (0, 1, 1, 4, 2, 2, 2, 4)
        if type(self.type) is not int or self.type not in range(1, 8) or type(self.layout) is not int or self.layout not in range(3):
            raise ValueError("invalid tensor type/layout")
        if not 1 <= len(self.shape) <= 4 or any(type(axis) is not int or not 0 < axis <= 0xffffffff for axis in self.shape):
            raise ValueError("invalid tensor shape")
        size = widths[self.type]
        for axis in self.shape:
            size *= axis
        if type(self.bytes) is not int or size != self.bytes or size > 0xffffffff or type(self.offset) is not int or not 0 <= self.offset <= 0xffffffff or self.offset % 32:
            raise ValueError("tensor size/offset mismatch")
        if not math.isfinite(self.scale) or self.scale <= 0 or type(self.role) is not int or not 0 <= self.role <= 255:
            raise ValueError("invalid tensor scale/role")
        limits = {1: (0, 255), 2: (-128, 127), 3: (0, 0), 4: (-32768, 32767), 5: (0, 65535), 6: (0, 0), 7: (-2147483648, 2147483647)}
        if type(self.zero) is not int or not limits[self.type][0] <= self.zero <= limits[self.type][1]:
            raise ValueError("invalid tensor zero point")

    def encode(self):
        self.validate()
        return TENSOR.pack(self.type, self.layout, len(self.shape), self.role,
                           *(self.shape + (0,) * (4 - len(self.shape))), self.bytes,
                           self.scale, self.zero, self.offset, 0)

    @classmethod
    def from_dict(cls, value):
        fields = dict(value)
        fields["type"] = TYPES.get(fields["type"], fields["type"])
        fields["layout"] = LAYOUTS.get(fields.get("layout", "raw"), fields.get("layout", 0))
        fields["shape"] = tuple(fields["shape"])
        if "bytes" not in fields:
            size = (0, 1, 1, 4, 2, 2, 2, 4)[fields["type"]]
            for axis in fields["shape"]:
                size *= axis
            fields["bytes"] = size
        return cls(**fields)


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
    tag: int = 0
    input: Tensor = None
    outputs: tuple = ()
    preprocessing: int = 0
    color: int = 0
    padding: int = 0
    mean: tuple = (0.0, 0.0, 0.0)
    divisor: tuple = (1.0, 1.0, 1.0)
    contract_hash: str = ""

    def validate(self):
        if self.contract_hash:
            if self.input is None or not isinstance(self.contract_hash, str) or len(self.contract_hash) != 64:
                raise ValueError("v3 requires a SHA-256 contract digest and tensor descriptors")
            try:
                digest = bytes.fromhex(self.contract_hash)
            except ValueError as error:
                raise ValueError("invalid contract digest") from error
            if len(digest) != 32 or not any(digest) or self.contract_hash != digest.hex():
                raise ValueError("invalid contract digest")
        if any(type(getattr(self, field)) is not int or not 0 <= getattr(self, field) <= 0xffffffff for field in FIELDS):
            raise ValueError("manifest fields must be uint32")
        if not all((self.runtime_version, self.kind, self.input_bytes, self.output_bytes, self.weights_bytes, self.blob_bytes)):
            raise ValueError("version, kind, tensor sizes and segment sizes must be nonzero")
        segments = [(self.weights_address, self.weights_bytes), (self.blob_address, self.blob_bytes)]
        if any(address + size > 0x100000000 for address, size in segments):
            raise ValueError("segment address overflow")
        if self.weights_address < self.blob_address + self.blob_bytes and self.blob_address < self.weights_address + self.weights_bytes:
            raise ValueError("segments overlap")
        if self.input is not None:
            if type(self.tag) is not int or not 0 < self.tag <= 0xffffffff or not 1 <= len(self.outputs) <= 8:
                raise ValueError("v2 requires code tag and 1..8 output tensors")
            if self.preprocessing not in range(3) or self.color not in range(4) or type(self.padding) is not int or self.padding not in range(256):
                raise ValueError("invalid preprocessing/color")
            if len(self.mean) != 3 or len(self.divisor) != 3 or any(not math.isfinite(value) for value in self.mean + self.divisor) or any(value <= 0 for value in self.divisor):
                raise ValueError("invalid normalization")
            self.input.validate()
            if self.input.offset or self.input.bytes != self.input_bytes:
                raise ValueError("input descriptor mismatch")
            end = 0
            for tensor in self.outputs:
                tensor.validate()
                if tensor.offset < end:
                    raise ValueError("output tensors overlap")
                end = tensor.offset + tensor.bytes
            if end != self.output_bytes:
                raise ValueError("output descriptor mismatch")
            if self.preprocessing and (len(self.input.shape) != 4 or self.input.shape[0] != 1
                                       or self.input.layout == 0 or self.color == 0
                                       or self.input.shape[3 if self.input.layout == 1 else 1] != (1 if self.color == 3 else 3)):
                raise ValueError("image preprocessing requires batch-one image tensor")
        elif self.tag or self.outputs:
            raise ValueError("v2 requires input descriptor")

    def encode(self):
        self.validate()
        descriptors = b""
        if self.input is not None:
            descriptors = struct.pack("<I4BI6f", self.tag, 1, len(self.outputs), self.preprocessing, self.color, self.padding,
                                      *self.mean, *self.divisor) + self.input.encode() + b"".join(tensor.encode() for tensor in self.outputs)
        if self.contract_hash:
            descriptors += bytes.fromhex(self.contract_hash)
        version = 3 if self.contract_hash else 2 if descriptors else 1
        prefix = PREFIX.pack(b"UAIM", version, SIZE + len(descriptors), *(getattr(self, field) for field in FIELDS)) + descriptors
        return prefix + CRC.pack(zlib.crc32(prefix))


def decode(data):
    if not SIZE <= len(data) <= 480:
        raise ValueError("invalid manifest size")
    magic, version, size, *fields = PREFIX.unpack(data[:48])
    if magic != b"UAIM" or version not in (1, 2, 3) or size != len(data) or CRC.unpack(data[-4:])[0] != zlib.crc32(data[:-4]):
        raise ValueError("invalid manifest header or checksum")
    manifest = Manifest(*fields)
    if version == 1 and size != SIZE:
        raise ValueError("invalid v1 header size")
    if version in (2, 3):
        if size < 168:
            raise ValueError("truncated tensor header")
        tag, inputs, outputs, preprocessing, color, padding, *normalization = struct.unpack("<I4BI6f", data[48:84])
        digest_bytes = 32 if version == 3 else 0
        if inputs != 1 or not 1 <= outputs <= 8 or padding > 255 or size != 88 + (inputs + outputs) * TENSOR.size + digest_bytes:
            raise ValueError("invalid tensor header counts")
        tensors = []
        for offset in range(84, size - 4 - digest_bytes, TENSOR.size):
            dtype, layout, rank, role, *values = TENSOR.unpack(data[offset:offset + TENSOR.size])
            if rank not in range(1, 5) or any(values[rank:4]) or values[-1]:
                raise ValueError("invalid tensor descriptor")
            tensors.append(Tensor(dtype, layout, tuple(values[:rank]), *values[4:8], role=role))
        manifest = dataclasses.replace(manifest, tag=tag, input=tensors[0], outputs=tuple(tensors[1:]),
                                       preprocessing=preprocessing, color=color, padding=padding, mean=tuple(normalization[:3]), divisor=tuple(normalization[3:]))
        if version == 3:
            manifest = dataclasses.replace(manifest, contract_hash=data[-36:-4].hex())
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