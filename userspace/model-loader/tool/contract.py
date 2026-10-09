import argparse
import dataclasses
import hashlib
import json
import pathlib
import re

from manifest import Tensor, decode, verify


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def execution_description(model):
    normalized = decode(model.encode())
    def describe(tensor, identifier):
        return {"id": identifier, **dataclasses.asdict(tensor)}
    value = {
        "backend": "stm32n6/st-ai", "runtime_version": normalized.runtime_version,
        "input": describe(normalized.input, "input0"),
        "outputs": [describe(tensor, f"output{index}") for index, tensor in enumerate(normalized.outputs)],
        "preprocessing": {"mode": normalized.preprocessing, "color": normalized.color, "padding": normalized.padding,
                          "mean": normalized.mean, "divisor": normalized.divisor}}
    return json.loads(canonical(value))


def validate(document):
    if not isinstance(document, dict) or set(document) != {"schema_version", "model_id", "execution", "semantics", "decoder"}:
        raise ValueError("unknown or missing required contract fields")
    if type(document["schema_version"]) is not int or document["schema_version"] != 1:
        raise ValueError("unsupported contract schema version")
    if not isinstance(document["model_id"], str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]{0,63}", document["model_id"]):
        raise ValueError("invalid model ID")
    execution = document["execution"]
    if not isinstance(execution, dict) or set(execution) != {"backend", "runtime_version", "input", "outputs", "preprocessing"}:
        raise ValueError("invalid execution contract")
    if execution["backend"] != "stm32n6/st-ai" or type(execution["runtime_version"]) is not int or execution["runtime_version"] <= 0:
        raise ValueError("unsupported execution backend/runtime")
    outputs = execution["outputs"]
    if not isinstance(outputs, list) or not 1 <= len(outputs) <= 8:
        raise ValueError("contract requires 1..8 output tensors")
    for index, tensor in enumerate([execution["input"], *outputs]):
        identifier = "input0" if index == 0 else f"output{index - 1}"
        if not isinstance(tensor, dict) or tensor.get("id") != identifier:
            raise ValueError("tensor IDs must be stable and ordered")
        fields = {key: value for key, value in tensor.items() if key != "id"}
        if set(fields) != {field.name for field in dataclasses.fields(Tensor)}:
            raise ValueError("invalid tensor descriptor fields")
        fields["shape"] = tuple(fields["shape"])
        Tensor(**fields).validate()
    semantics = document["semantics"]
    if not isinstance(semantics, dict) or set(semantics) != {"task", "input_domain", "outputs", "labels"}:
        raise ValueError("invalid semantic contract")
    if any(not isinstance(semantics[key], str) or not semantics[key] for key in ("task", "input_domain")):
        raise ValueError("task and input domain must be explicit, including unknown")
    identifiers = [tensor["id"] for tensor in outputs]
    meanings = semantics["outputs"]
    if not isinstance(meanings, list) or len(meanings) != len(outputs):
        raise ValueError("each output requires a semantic descriptor")
    for tensor, meaning in zip(outputs, meanings):
        allowed = {"id", "meaning", "axes", "coordinates", "score_domain"}
        if not isinstance(meaning, dict) or set(meaning) - allowed or meaning.get("id") != tensor["id"]:
            raise ValueError("semantic output binding mismatch")
        if not isinstance(meaning.get("meaning"), str) or not meaning["meaning"]:
            raise ValueError("output meaning must be explicit, including unknown")
        if "axes" in meaning and (not isinstance(meaning["axes"], list) or len(meaning["axes"]) != len(tensor["shape"])
                                  or any(not isinstance(axis, str) or not axis for axis in meaning["axes"])):
            raise ValueError("semantic axes must describe every tensor dimension")
    labels = semantics["labels"]
    if not isinstance(labels, list):
        raise ValueError("labels must be a list")
    seen = set()
    for label in labels:
        if not isinstance(label, dict) or set(label) != {"id", "name"} or type(label["id"]) is not int or label["id"] < 0:
            raise ValueError("invalid label definition")
        if label["id"] in seen or not isinstance(label["name"], str) or not label["name"]:
            raise ValueError("labels require unique IDs and nonempty names")
        seen.add(label["id"])
    decoder = document["decoder"]
    if not isinstance(decoder, dict) or set(decoder) != {"id", "version", "bindings", "parameters", "result_schema"}:
        raise ValueError("invalid decoder contract")
    if any(not isinstance(decoder[key], str) or not decoder[key] for key in ("id", "result_schema")):
        raise ValueError("decoder ID and result schema are required")
    if type(decoder["version"]) is not int or decoder["version"] < 1 or not isinstance(decoder["bindings"], dict) or not isinstance(decoder["parameters"], dict):
        raise ValueError("invalid decoder version/bindings/parameters")
    if any(not isinstance(key, str) or value not in identifiers for key, value in decoder["bindings"].items()):
        raise ValueError("decoder references an unknown output")
    if decoder["id"] == "raw" and decoder["version"] == 1:
        if decoder["result_schema"] != "raw_tensors/v1" or decoder["bindings"] or decoder["parameters"]:
            raise ValueError("raw decoder has no interpretation parameters")
    canonical(document)
    return document


def create(model_id, model, source=None):
    execution = execution_description(model)
    if source is None:
        source = {
            "semantics": {"task": "unknown", "input_domain": "unknown",
                          "outputs": [{"id": tensor["id"], "meaning": "unknown"} for tensor in execution["outputs"]], "labels": []},
            "decoder": {"id": "raw", "version": 1, "bindings": {}, "parameters": {}, "result_schema": "raw_tensors/v1"}}
    if not isinstance(source, dict) or set(source) != {"semantics", "decoder"}:
        raise ValueError("semantic source requires semantics and decoder")
    return validate({"schema_version": 1, "model_id": model_id, "execution": execution, **source})


def write_metadata(directory, document, header, weights, blob):
    (directory / "contract.json").write_bytes(canonical(document) + b"\n")
    artifacts = {name: hashlib.sha256(data).hexdigest() for name, data in
                 (("manifest.bin", header), ("weights.bin", weights), ("blob.bin", blob))}
    (directory / "package.json").write_bytes(canonical({"package_version": 1, "contract_sha256": digest(document), "artifacts": artifacts}) + b"\n")


def verify_package(manifest_path, weights_path=None, blob_path=None):
    header = manifest_path.read_bytes()
    model = decode(header)
    if not model.contract_hash:
        return None
    directory = manifest_path.parent
    document = validate(json.loads((directory / "contract.json").read_text()))
    if digest(document) != model.contract_hash or document["execution"] != execution_description(model):
        raise ValueError("Header and semantic contract do not match")
    package = json.loads((directory / "package.json").read_text())
    if not isinstance(package, dict) or set(package) != {"package_version", "contract_sha256", "artifacts"} or package["package_version"] != 1:
        raise ValueError("unsupported package metadata")
    if package["contract_sha256"] != model.contract_hash or set(package["artifacts"]) != {"manifest.bin", "weights.bin", "blob.bin"}:
        raise ValueError("package contract/artifact mismatch")
    weights = (weights_path or directory / "weights.bin").read_bytes()
    blob = (blob_path or directory / "blob.bin").read_bytes()
    if not verify(model, weights, blob):
        raise ValueError("package payload CRC mismatch")
    for name, data in (("manifest.bin", header), ("weights.bin", weights), ("blob.bin", blob)):
        if hashlib.sha256(data).hexdigest() != package["artifacts"][name]:
            raise ValueError("package artifact digest mismatch: " + name)
    return document


def changed_paths(left, right, prefix=""):
    if left == right:
        return []
    if isinstance(left, dict) and isinstance(right, dict):
        return [path for key in sorted(set(left) | set(right))
                for path in changed_paths(left.get(key), right.get(key), prefix + "/" + key)]
    if isinstance(left, list) and isinstance(right, list) and len(left) == len(right):
        return [path for index, (before, after) in enumerate(zip(left, right))
                for path in changed_paths(before, after, prefix + "/" + str(index))]
    return [prefix or "/"]


def compare(left, right):
    validate(left)
    validate(right)
    groups = {key: changed_paths(left[key], right[key]) for key in ("model_id", "execution", "semantics", "decoder")}
    return {"same_execution_contract": not groups["execution"],
            "same_meaning_contract": not groups["semantics"] and not groups["decoder"], "changes": groups}


def main(argv=None):
    parser = argparse.ArgumentParser(prog="model-loader contract")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("inspect", "verify"):
        command = commands.add_parser(name)
        command.add_argument("package", type=pathlib.Path)
    difference = commands.add_parser("diff")
    difference.add_argument("before", type=pathlib.Path)
    difference.add_argument("after", type=pathlib.Path)
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "diff":
            before = verify_package(arguments.before / "manifest.bin")
            after = verify_package(arguments.after / "manifest.bin")
            if before is None or after is None:
                raise ValueError("contract comparison requires v3 packages")
            result = compare(before, after)
            before_artifacts = json.loads((arguments.before / "package.json").read_text())["artifacts"]
            after_artifacts = json.loads((arguments.after / "package.json").read_text())["artifacts"]
            result["artifact_changes"] = changed_paths(before_artifacts, after_artifacts)
        else:
            result = verify_package(arguments.package / "manifest.bin")
            if result is None:
                raise ValueError("semantic contracts require a v3 package")
            if arguments.command == "verify":
                result = {"status": "verified", "model_id": result["model_id"], "contract_sha256": digest(result)}
        print(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()