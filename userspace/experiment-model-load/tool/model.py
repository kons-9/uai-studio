import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import zlib

from expected import generate as expected_header
from layout import BLOB_ADDRESS, BLOB_CAPACITY, IO_CAPACITY, WEIGHTS_ADDRESS, WEIGHTS_CAPACITY
from manifest import Manifest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIELDS = ("runtime_version", "kind", "input_bytes", "output_bytes")


def read_contract(path):
    value = json.loads(path.read_text())
    if not isinstance(value, dict) or set(value) != set(FIELDS):
        raise ValueError("contract requires runtime_version, kind, input_bytes and output_bytes")
    if any(type(value[field]) is not int or not 0 < value[field] <= 0xffffffff for field in FIELDS):
        raise ValueError("contract fields must be positive uint32 values")
    if value["input_bytes"] > IO_CAPACITY or value["output_bytes"] > IO_CAPACITY:
        raise ValueError("experiment input/output buffers exceed the reserved SRAM capacity")
    return value


def package(model_dir, blob, output_dir):
    contract = read_contract(model_dir / "contract.json")
    weights = (model_dir / "weights.bin").read_bytes()
    value = Manifest(**contract, weights_address=WEIGHTS_ADDRESS, weights_bytes=len(weights),
                     weights_crc=zlib.crc32(weights), blob_address=BLOB_ADDRESS,
                     blob_bytes=len(blob), blob_crc=zlib.crc32(blob))
    header = value.encode()
    source = expected_header(header)
    if output_dir.resolve() == model_dir.resolve():
        raise ValueError("package output must not overwrite generated model sources")
    if len(weights) > WEIGHTS_CAPACITY or len(blob) > BLOB_CAPACITY:
        raise ValueError("model weights/blob exceed the reserved PSRAM slots")
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in (("weights.bin", weights), ("blob.bin", blob), ("manifest.bin", header)):
        (output_dir / name).write_bytes(data)
    (output_dir / "model_expected.hpp").write_text(source)
    return value


def generate(arguments):
    source = arguments.model.resolve(strict=True)
    output = ROOT / "models" / "generated"
    if output.resolve() in source.parents:
        raise ValueError("source model must not be inside the generated output directory")
    contract = {field: getattr(arguments, field) for field in FIELDS}
    with tempfile.TemporaryDirectory(prefix="model-load-") as temporary:
        work = pathlib.Path(temporary)
        (work / "contract.json").write_text(json.dumps(contract))
        read_contract(work / "contract.json")
        pools = ROOT / "config" / "model.mpool"
        profile = {"Globals": {}, "Profiles": {"default": {
            "memory_pool": str(pools),
            "options": "-O3 --all-buffers-info --cache-maintenance --Os --enable-epoch-controller"
        }}}
        config = work / "neuralart.json"
        config.write_text(json.dumps(profile))
        generated = work / "output"
        subprocess.run([arguments.stedgeai, "generate", "--target", "stm32n6", "--model", str(source),
                        "--c-api", "st-ai", "--no-inputs-allocation", "--no-outputs-allocation",
                        "--optimization", "balanced", "--memory-pool", str(pools),
                        "--st-neural-art", f"default@{config}", "--workspace", str(work / "workspace"),
                        "--output", str(generated)], check=True)
        names = ("network.c", "network_ecblobs.h", "stai_network.c", "stai_network.h")
        for name in names + ("network_atonbuf.weights.raw",):
            if not (generated / name).is_file():
                raise ValueError("STEdgeAI did not generate " + name)
        if not 0 < (generated / "network_atonbuf.weights.raw").stat().st_size <= WEIGHTS_CAPACITY:
            raise ValueError("generated weights exceed the reserved PSRAM slot")
        output.mkdir(parents=True, exist_ok=True)
        for name in names:
            shutil.copyfile(generated / name, output / name)
        shutil.copyfile(generated / "network_atonbuf.weights.raw", output / "weights.bin")
        shutil.copyfile(work / "contract.json", output / "contract.json")
    print("Generated local PSRAM model:", output)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    generation = commands.add_parser("generate")
    generation.add_argument("--model", type=pathlib.Path, required=True)
    generation.add_argument("--stedgeai", default="stedgeai")
    for field in FIELDS:
        generation.add_argument("--" + field.replace("_", "-"), type=lambda value: int(value, 0), required=True)
    contract = commands.add_parser("contract")
    contract.add_argument("path", type=pathlib.Path)
    packing = commands.add_parser("package")
    packing.add_argument("--model-dir", type=pathlib.Path, required=True)
    packing.add_argument("--object", type=pathlib.Path, required=True)
    packing.add_argument("--objcopy", required=True)
    packing.add_argument("--objdump", required=True)
    packing.add_argument("--output-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.command == "generate":
            generate(arguments)
        elif arguments.command == "contract":
            value = read_contract(arguments.path)
            print(";".join(str(value[field]) for field in FIELDS))
        else:
            relocations = subprocess.run([arguments.objdump, "-r", "-j", ".model_command_blob", str(arguments.object)],
                                         capture_output=True, text=True, check=True).stdout
            if re.search(r"^[0-9a-fA-F]{4,}\s+\S+\s+\S+", relocations, re.MULTILINE):
                raise ValueError("command blob contains link relocations; cannot package object bytes")
            with tempfile.TemporaryDirectory(prefix="model-blob-") as temporary:
                blob = pathlib.Path(temporary) / "blob.bin"
                subprocess.run([arguments.objcopy, "-O", "binary", "--only-section=.model_command_blob",
                                str(arguments.object), str(blob)], check=True)
                package(arguments.model_dir, blob.read_bytes(), arguments.output_dir)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
