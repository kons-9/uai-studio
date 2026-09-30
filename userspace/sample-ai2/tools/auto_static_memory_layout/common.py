"""Shared parsing and validation helpers."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .layout_core import (
    LayoutError,
    generate_memory_regions,
    generate_key_header,
    generate_raw_header,
    generate_static_sections,
    load_document,
    normalize_layout,
    parse_size,
    write_if_changed,
)


def required(mapping: dict[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise LayoutError(f"{context} is missing required key {key!r}")
    return mapping[key]


def read_document(path: Path) -> dict[str, Any]:
    return load_document(path, None)


def parse_int(value: Any, context: str) -> int:
    return parse_size(value, context)


def get_path(document: dict[str, Any], dotted: str, context: str) -> Any:
    value: Any = document
    for component in dotted.split("."):
        if not isinstance(value, dict) or component not in value:
            raise LayoutError(f"{context} references unknown value {dotted!r}")
        value = value[component]
    return value


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise LayoutError(
            f"alignment must be a positive power of two, got {alignment}"
        )
    return (value + alignment - 1) // alignment * alignment


def format_template(value: str, index: int) -> str:
    return value.replace("{index}", str(index))
