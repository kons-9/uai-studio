"""Extract model metadata from generated STEdgeAI artifacts."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from .common import LayoutError, align_up, parse_int


def _macro_int(text: str, name: str, context: str) -> int:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+\((\d+)\)",
        text,
        re.MULTILINE,
    )
    if not match:
        raise LayoutError(f"{context} is missing generated macro {name}")
    return int(match.group(1), 10)


def _macro_value(text: str, name: str, context: str) -> str:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+(?:\(([^)\n]+)\)|([^\n]+))",
        text,
        re.MULTILINE,
    )
    if not match:
        raise LayoutError(f"{context} is missing generated macro {name}")
    value = (match.group(1) or match.group(2)).strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        try:
            return str(json.loads(value))
        except json.JSONDecodeError as error:
            raise LayoutError(f"{context} has invalid string macro {name}") from error
    return value


def _parse_shape(text: str, prefix: str, index: int) -> list[int]:
    match = re.search(
        rf"^#define\s+{re.escape(prefix)}_{index}_SHAPE\s*\\\s*\{{(.*?)\}}",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        return []
    return [int(value) for value in re.findall(r"\d+", match.group(1))]


def _parse_tensor(
    text: str, kind: str, index: int, context: str
) -> dict[str, Any]:
    prefix = f"STAI_NETWORK_{kind}"
    return {
        "name": _macro_value(text, f"{prefix}_{index}_NAME", context),
        "format": _macro_value(text, f"{prefix}_{index}_FORMAT", context),
        "size_bytes": _macro_int(
            text, f"{prefix}_{index}_SIZE_BYTES", context
        ),
        "alignment": _macro_int(text, f"{prefix}_{index}_ALIGNMENT", context),
        "shape": _parse_shape(text, prefix, index),
    }


def parse_model_header(path: Path, model_name: str) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    context = f"{model_name}:{path}"
    input_count = _macro_int(text, "STAI_NETWORK_IN_NUM", context)
    output_count = _macro_int(text, "STAI_NETWORK_OUT_NUM", context)
    return {
        "inputs": [
            _parse_tensor(text, "IN", index, context)
            for index in range(1, input_count + 1)
        ],
        "outputs": [
            _parse_tensor(text, "OUT", index, context)
            for index in range(1, output_count + 1)
        ],
        "counts": {
            "inputs": input_count,
            "outputs": output_count,
            "weights": _macro_int(text, "STAI_NETWORK_WEIGHTS_NUM", context),
        },
    }


def parse_npu_pools(path: Path) -> list[dict[str, Any]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    pools: list[dict[str, Any]] = []
    for index, line in enumerate(lines[:-1]):
        global_match = re.match(r"/\* global pool (\d+) is (.*?) \*/", line)
        if not global_match:
            continue
        detail = re.match(
            r"/\* index=(\d+) file postfix=(\S+) name=(\S+) "
            r"offset=(0x[0-9a-fA-F]+).*? size=(\d+)",
            lines[index + 1],
        )
        if not detail:
            continue
        pool_index, postfix, name, origin, capacity = detail.groups()
        pools.append(
            {
                "index": int(pool_index),
                "reported_required": global_match.group(2),
                "postfix": postfix,
                "name": name,
                "origin": origin,
                "capacity_bytes": int(capacity),
            }
        )
    return pools


def parse_command_blob_size(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    words = [
        int(word_count)
        for word_count in re.findall(
            r"^static const uint64_t _ec_blob_network_\d+\[(\d+)\]",
            text,
            re.MULTILINE,
        )
    ]
    if not words:
        raise LayoutError(f"no ECBLOB command blob arrays found in {path}")
    offset = 0
    for word_count in words:
        offset = align_up(offset, 64)
        offset += word_count * 8
    return offset


def resolve_models(
    root: Path,
    models_dir: Path,
    model_config: dict[str, Any],
    board: dict[str, Any],
) -> list[dict[str, Any]]:
    model_names = model_config.get("model_order")
    if model_names is None:
        model_names = list(model_config.get("models", {}).keys())
    if not isinstance(model_names, list) or not model_names:
        raise LayoutError("model configuration must contain a non-empty model_order")

    model_specs = model_config.get("models", {})
    addresses = model_config.get("weight_addresses", {})
    command_specs = {item["model"]: item for item in board.get("command_blobs", [])}
    result: list[dict[str, Any]] = []
    for model_id, model_name in enumerate(model_names):
        if model_name not in model_specs:
            raise LayoutError(f"model configuration is missing {model_name!r}")
        model_dir = models_dir / model_name
        header = model_dir / "stai_network.h"
        source = model_dir / "network.c"
        blob_header = model_dir / "network_ecblobs.h"
        weights = model_dir / "network_data.xSPI2.bin"
        for artifact in (header, source, blob_header, weights):
            if not artifact.exists():
                raise LayoutError(
                    f"generated model artifact is missing: {artifact}. "
                    f"Run models/generate_model.sh {model_name} first."
                )
        if model_name not in addresses:
            raise LayoutError(f"weight_addresses is missing {model_name!r}")
        if model_name not in command_specs:
            raise LayoutError(f"command_blobs is missing {model_name!r}")

        io = parse_model_header(header, model_name)
        command = command_specs[model_name]
        command_capacity = parse_int(
            command["capacity"], f"command_blobs[{model_name}].capacity"
        )
        command_size = parse_command_blob_size(blob_header)
        if command_size > command_capacity:
            raise LayoutError(
                f"{model_name} command blob needs {command_size:#x}, "
                f"but its slot is only {command_capacity:#x}"
            )
        result.append(
            {
                "name": model_name,
                "model_id": model_id,
                "source_config": model_specs[model_name],
                "artifacts": {
                    "network_header": str(header.relative_to(root)),
                    "network_source": str(source.relative_to(root)),
                    "blob_header": str(blob_header.relative_to(root)),
                    "weights": str(weights.relative_to(root)),
                },
                "weights": {
                    "address": str(addresses[model_name]),
                    "size_bytes": weights.stat().st_size,
                },
                "io": io,
                "command_blob": {
                    "section": command["section"],
                    "address": str(command["address"]),
                    "capacity_bytes": command_capacity,
                    "alignment": int(command.get("alignment", 64)),
                    "size_bytes": command_size,
                },
                "npu_memory": {
                    "memory_pool": str(
                        (
                            models_dir
                            / "my_mpools"
                            / "stm32n6-app2_STM32N6570-DK.mpool"
                        ).relative_to(root)
                    ),
                    "pools": parse_npu_pools(source),
                },
            }
        )
    return result

