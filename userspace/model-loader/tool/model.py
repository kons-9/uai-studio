import argparse
import dataclasses
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import zlib

from expected import generate as expected_header
from layout import BLOB_ADDRESS, BLOB_CAPACITY, IO_CAPACITY, WEIGHTS_ADDRESS, WEIGHTS_CAPACITY
from manifest import Manifest, Tensor
import contract as model_contract

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIELDS = ("runtime_version", "kind", "input_bytes", "output_bytes")
KINDS = {"person": 1, "face": 2, "seg": 3}
SYMBOLS = ("init", "deinit", "run", "train", "get_info", "get_inputs", "get_weights", "get_outputs",
           "get_activations", "get_states", "get_error", "set_inputs", "set_weights", "set_outputs",
           "set_activations", "set_states", "set_callback")
ATON_SYMBOLS = ("WeightEncryption_Info", "BlobEncryption_Info", "Set_User_Input_Buffer", "Get_User_Input_Buffer",
                "Set_User_Output_Buffer", "Get_User_Output_Buffer", "EpochBlockItems", "Input_Buffers_Info",
                "Output_Buffers_Info", "Internal_Buffers_Info", "EC_Network_Init", "EC_Inference_Init")


def descriptors(value):
    source = dict(value["input"])
    source.setdefault("offset", 0)
    inputs = Tensor.from_dict(source)
    outputs, end = [], 0
    for item in value["outputs"]:
        item = dict(item)
        item.setdefault("offset", (end + 31) & ~31)
        tensor = Tensor.from_dict(item)
        outputs.append(tensor)
        end = tensor.offset + tensor.bytes
    return dict(input=inputs, outputs=tuple(outputs), preprocessing=value.get("preprocessing", 0),
                color=value.get("color", 0), mean=tuple(value.get("mean", [0, 0, 0])),
                divisor=tuple(value.get("divisor", [1, 1, 1])), padding=value.get("padding", 0))


def read_contract(path):
    value = json.loads(path.read_text())
    if isinstance(value, dict) and "input" in value and "outputs" in value:
        allowed = {"runtime_version", "kind", "input", "outputs", "preprocessing", "color", "mean", "divisor", "padding"}
        if set(value) - allowed:
            raise ValueError("unknown tensor contract fields")
        metadata = descriptors(value)
        value = dict(runtime_version=value["runtime_version"], kind=value["kind"], input_bytes=metadata["input"].bytes,
                     output_bytes=metadata["outputs"][-1].offset + metadata["outputs"][-1].bytes, **metadata)
        Manifest(**value, tag=1, weights_address=WEIGHTS_ADDRESS, weights_bytes=1, weights_crc=0,
                 blob_address=BLOB_ADDRESS, blob_bytes=1, blob_crc=0).validate()
    elif not isinstance(value, dict) or set(value) != set(FIELDS):
        raise ValueError("contract requires runtime_version, kind, input_bytes and output_bytes")
    if any(type(value[field]) is not int or not 0 < value[field] <= 0xffffffff for field in FIELDS):
        raise ValueError("contract fields must be positive uint32 values")
    if value["input_bytes"] > (0x200000 if "input" in value else IO_CAPACITY) or value["output_bytes"] > (0x400000 if "input" in value else IO_CAPACITY):
        raise ValueError("experiment input/output buffers exceed the reserved SRAM capacity")
    return value


def package(model_dir, blob, output_dir):
    contract = read_contract(model_dir / "contract.json")
    if "input" in contract:
        contract["tag"] = zlib.crc32((model_dir / "network.c").read_bytes()) or 1
    weights = (model_dir / "weights.bin").read_bytes()
    value = Manifest(**contract, weights_address=WEIGHTS_ADDRESS, weights_bytes=len(weights),
                     weights_crc=zlib.crc32(weights), blob_address=BLOB_ADDRESS,
                     blob_bytes=len(blob), blob_crc=zlib.crc32(blob))
    description = None
    if value.input is not None:
        semantics_path = model_dir / "semantics.json"
        semantics = json.loads(semantics_path.read_text()) if semantics_path.exists() else None
        description = model_contract.create(model_dir.name, value, semantics)
        value = dataclasses.replace(value, contract_hash=model_contract.digest(description))
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
    if description is not None:
        model_contract.write_metadata(output_dir, description, header, weights, blob)
    return value


def generate(arguments):
    source = arguments.model.resolve(strict=True)
    output = ROOT / "models" / (arguments.name or "generated")
    if output.resolve() in source.parents:
        raise ValueError("source model must not be inside the generated output directory")
    contract = json.loads(arguments.descriptor.read_text()) if arguments.descriptor else {field: getattr(arguments, field) for field in FIELDS}
    if arguments.name:
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", arguments.name) or not arguments.descriptor:
            raise ValueError("named models require a valid C identifier and tensor descriptor JSON")
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
        if arguments.semantics and arguments.semantics.resolve() != (output / "semantics.json").resolve():
            shutil.copyfile(arguments.semantics, output / "semantics.json")
        if arguments.name:
            symbols = [f"#define stai_network_{symbol} {arguments.name}_stai_network_{symbol}" for symbol in SYMBOLS]
            symbols += [f"#define stai_ext_network_{symbol} {arguments.name}_stai_ext_network_{symbol}"
                        for symbol in ("run_continue", "get_nn_run_status", "new_inference")]
            symbols += [f"#define LL_ATON_{symbol}_network {arguments.name}_LL_ATON_{symbol}_network" for symbol in ATON_SYMBOLS]
            (output / "model_symbols.h").write_text("\n".join(symbols) + "\n")
    print("Generated local PSRAM model:", output)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    generation = commands.add_parser("generate")
    generation.add_argument("--model", type=pathlib.Path, required=True)
    generation.add_argument("--stedgeai", default="stedgeai")
    generation.add_argument("--name")
    generation.add_argument("--semantics", type=pathlib.Path)
    generation.add_argument("--descriptor", type=pathlib.Path)
    for field in FIELDS:
        generation.add_argument("--" + field.replace("_", "-"), type=lambda value: int(value, 0))
    contract = commands.add_parser("contract")
    contract.add_argument("path", type=pathlib.Path)
    contract.add_argument("--tensors", action="store_true")
    packing = commands.add_parser("package")
    packing.add_argument("--model-dir", type=pathlib.Path, required=True)
    packing.add_argument("--object", type=pathlib.Path, required=True)
    packing.add_argument("--objcopy", required=True)
    packing.add_argument("--objdump", required=True)
    packing.add_argument("--output-dir", type=pathlib.Path, required=True)
    packing.add_argument("--section", default=".model_command_blob")
    catalog = commands.add_parser("catalog")
    catalog.add_argument("--packages", type=pathlib.Path, nargs="+", required=True)
    catalog.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.command == "generate":
            generate(arguments)
        elif arguments.command == "catalog":
            headers = [(directory / "manifest.bin").read_bytes() for directory in arguments.packages]
            values = []
            for index, header in enumerate(headers):
                expected_header(header)
                values.append(f"inline constexpr unsigned char catalog_header_{index}[] = {{" + ",".join(map(str, header)) + "};")
            values += ["inline constexpr const unsigned char *catalog_headers[] = {" + ",".join(f"catalog_header_{index}" for index in range(len(headers))) + "};",
                       "inline constexpr unsigned catalog_sizes[] = {" + ",".join(str(len(header)) for header in headers) + "};",
                       f"inline constexpr unsigned catalog_count = {len(headers)};", "inline constexpr auto &expected_header = catalog_header_0;"]
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_text("#pragma once\n" + "\n".join(values) + "\n")
        elif arguments.command == "contract":
            value = read_contract(arguments.path)
            if arguments.tensors:
                print(";".join(map(str, [value["input_bytes"], len(value["outputs"]), *(tensor.bytes for tensor in value["outputs"])])))
            else:
                print(";".join(str(value[field]) for field in FIELDS))
        else:
            relocations = subprocess.run([arguments.objdump, "-r", "-j", arguments.section, str(arguments.object)],
                                         capture_output=True, text=True, check=True).stdout
            if re.search(r"^[0-9a-fA-F]{4,}\s+\S+\s+\S+", relocations, re.MULTILINE):
                raise ValueError("command blob contains link relocations; cannot package object bytes")
            with tempfile.TemporaryDirectory(prefix="model-blob-") as temporary:
                blob = pathlib.Path(temporary) / "blob.bin"
                subprocess.run([arguments.objcopy, "-O", "binary", "--only-section=" + arguments.section,
                                str(arguments.object), str(blob)], check=True)
                package(arguments.model_dir, blob.read_bytes(), arguments.output_dir)
    except (OSError, ValueError, KeyError, IndexError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
