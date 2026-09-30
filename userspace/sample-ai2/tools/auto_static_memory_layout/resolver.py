"""Resolve board, application, and model inputs into canonical JSON."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .common import (
    LayoutError,
    align_up,
    format_template,
    get_path,
    parse_int,
    read_document,
    required,
)
from .model_artifacts import resolve_models


def resolve_document(
    root: Path,
    board_path: Path,
    application_path: Path,
    models_dir: Path,
    model_config_path: Path,
) -> dict[str, Any]:
    board = read_document(board_path)
    application = read_document(application_path)
    model_config = read_document(model_config_path)
    models = resolve_models(root, models_dir, model_config, board)

    alignment = int(application.get("alignment", 32))
    runtime = required(application, "runtime", "application specification")

    def format_bytes(name: str) -> int:
        value = required(runtime, name, f"runtime.{name}")
        return (
            int(value["width"])
            * int(value["height"])
            * int(value["bytes_per_pixel"])
        )

    capture_bytes = format_bytes("capture")
    display_bytes = format_bytes("display")
    inference_bytes = format_bytes("inference")
    scratch_bytes = format_bytes("inference_scratch")
    output_slots: list[int] = []
    for model in models:
        for index, output in enumerate(model["io"]["outputs"]):
            while len(output_slots) <= index:
                output_slots.append(0)
            output_slots[index] = max(output_slots[index], int(output["size_bytes"]))
    if not output_slots:
        raise LayoutError("models do not contain any output tensors")
    aligned_output_slots = [align_up(value, alignment) for value in output_slots]
    output_storage = sum(aligned_output_slots)
    outputs_offset = align_up(inference_bytes, alignment)
    inference_required = align_up(outputs_offset + output_storage, alignment)
    derived = {
        "capture_frame_bytes": capture_bytes,
        "display_frame_bytes": display_bytes,
        "inference_frame_bytes": inference_bytes,
        "inference_source_bytes": inference_bytes,
        "inference_scratch_bytes": scratch_bytes,
        "inference_output_bytes": output_slots,
        "inference_output_aligned_bytes": aligned_output_slots,
        "inference_output_storage_bytes": output_storage,
        "inference_outputs_offset": outputs_offset,
        "inference_buffer_bytes": inference_required,
        "inference_buffer_reserved_bytes": None,
    }

    document: dict[str, Any] = {
        "schema_version": 1,
        "header_guard": board.get(
            "header_guard", "UAI_AI_STATIC_MEMORY_LAYOUT_HPP"
        ),
        "raw_header_guard": board.get(
            "raw_header_guard", "UAI_AI_STATIC_MEMORY_LAYOUT_RAW_HPP"
        ),
        "cpp_namespace": board.get(
            "cpp_namespace", "uai::ai::static_memory_layout"
        ),
        "symbol_prefix": board.get("symbol_prefix", "sample_ai"),
        "board": board.get("board", "unknown"),
        "memory_regions": board["memory_regions"],
        "models": models,
        "runtime": runtime,
        "allocator_policy": {
            "alignment": alignment,
            "capture_count": int(runtime["capture"]["count"]),
            "display_count": int(runtime["display"]["count"]),
            "inference_count": int(runtime["inference"]["count"]),
            "inference_source_count": int(runtime["inference_source"]["count"]),
        },
        "derived": derived,
        "command_blobs": board.get("command_blobs", []),
        "linker": {"entry": board.get("linker_entry", "uai_ram_entry")},
    }

    allocations: list[dict[str, Any]] = []
    for reservation in required(
        application, "reservations", "application specification"
    ):
        context = f"reservations[{len(allocations)}]"
        if not isinstance(reservation, dict):
            raise LayoutError(f"{context} must be an object")
        count = int(
            get_path(document, reservation["count_ref"], context)
            if "count_ref" in reservation
            else reservation.get("count", 1)
        )
        if count <= 0:
            raise LayoutError(f"{context}.count must be positive")
        if "size_ref" in reservation:
            size = int(get_path(document, reservation["size_ref"], context))
        else:
            size = parse_int(reservation["size"], f"{context}.size")
        if size <= 0:
            raise LayoutError(f"{context}.size must be positive")
        for index in range(count):
            name = reservation["name"]
            if count > 1:
                name = f"{name}{index}"
            allocations.append(
                {
                    "name": name,
                    "key": format_template(reservation["key"], index),
                    "memory": reservation["memory"],
                    "section": reservation["section"],
                    "size": f"0x{size:X}",
                    "alignment": int(reservation.get("alignment", alignment)),
                    "noload": bool(reservation.get("noload", True)),
                }
            )
    document["allocations"] = allocations

    def indexed_region_count(prefix: str) -> int:
        pattern = re.compile(rf"^{re.escape(prefix)}(?P<index>[0-9]+)$")
        indices = sorted(
            int(match.group("index"))
            for allocation in allocations
            if (match := pattern.fullmatch(str(allocation["key"])))
        )
        if not indices:
            raise LayoutError(
                f"application specification must reserve at least one {prefix}N region"
            )
        expected = list(range(len(indices)))
        if indices != expected:
            raise LayoutError(
                f"{prefix} region keys must be contiguous from index 0, got {indices}"
            )
        return len(indices)

    inference_region_count = indexed_region_count("kInference")
    inference_source_region_count = indexed_region_count("kInferenceSource")
    if inference_region_count != int(runtime["inference"]["count"]):
        raise LayoutError(
            "inference region count does not match runtime.inference.count: "
            f"{inference_region_count} != {runtime['inference']['count']}"
        )
    if inference_source_region_count != int(runtime["inference_source"]["count"]):
        raise LayoutError(
            "inference source region count does not match "
            "runtime.inference_source.count: "
            f"{inference_source_region_count} != "
            f"{runtime['inference_source']['count']}"
        )
    if inference_region_count != inference_source_region_count:
        raise LayoutError(
            "inference and inference source region counts must match for indexed "
            "inference frames"
        )
    document["allocator_policy"]["inference_count"] = inference_region_count
    document["allocator_policy"]["inference_source_count"] = (
        inference_source_region_count
    )
    document["derived"]["inference_region_count"] = inference_region_count
    document["derived"]["inference_source_region_count"] = (
        inference_source_region_count
    )

    inference_reservation = next(
        (allocation for allocation in allocations if allocation["name"] == "inference0"),
        None,
    )
    if inference_reservation is None:
        raise LayoutError("application specification must reserve inference0")
    reserved = parse_int(
        inference_reservation["size"], "allocations[inference0].size"
    )
    document["derived"]["inference_buffer_reserved_bytes"] = reserved
    if inference_required > reserved:
        raise LayoutError(
            f"inference buffer needs {inference_required:#x}, "
            f"but the reserved slot is only {reserved:#x}"
        )

    required_by_name = {
        "capture": capture_bytes,
        "display": display_bytes,
        "inference": inference_required,
        "inference_scratch": scratch_bytes,
        "inference_source": inference_bytes,
    }
    for name, required_bytes in required_by_name.items():
        matching = [
            allocation
            for allocation in allocations
            if allocation["name"] == name
            or (
                allocation["name"].startswith(name)
                and allocation["name"][len(name) :].isdigit()
            )
        ]
        if not matching:
            raise LayoutError(f"application specification must reserve {name}")
        for allocation in matching:
            allocation_size = parse_int(
                allocation["size"], f"allocations[{allocation['name']}].size"
            )
            if allocation_size < required_bytes:
                raise LayoutError(
                    f"{allocation['name']} needs {required_bytes:#x}, "
                    f"but its reservation is only {allocation_size:#x}"
                )
    return document
