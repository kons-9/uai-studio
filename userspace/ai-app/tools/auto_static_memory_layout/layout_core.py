"""Core parser, validator, and emitters used by the package CLI."""

from __future__ import annotations

import ast
import json
import re
from pathlib import Path
from typing import Any, Iterable


SUPPORTED_FORMATS = {"json", "toml", "yaml", "yml"}
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SIZE_RE = re.compile(
    r"^(?P<number>(?:0[xX][0-9a-fA-F]+|[0-9]+))(?:\s*(?P<unit>[kKmMgG]))?$"
)


class LayoutError(ValueError):
    """An actionable error in the layout input or template."""


def _strip_yaml_comment(value: str) -> str:
    quote: str | None = None
    escaped = False
    for index, char in enumerate(value):
        if quote:
            if quote == '"' and char == "\\" and not escaped:
                escaped = True
                continue
            if char == quote and not escaped:
                quote = None
            escaped = False
            continue
        if char in "'\"":
            quote = char
        elif char == "#" and (index == 0 or value[index - 1].isspace()):
            return value[:index].rstrip()
    return value.rstrip()


def _split_yaml_key(value: str) -> tuple[str, str] | None:
    quote: str | None = None
    escaped = False
    for index, char in enumerate(value):
        if quote:
            if quote == '"' and char == "\\" and not escaped:
                escaped = True
                continue
            if char == quote and not escaped:
                quote = None
            escaped = False
            continue
        if char in "'\"":
            quote = char
        elif char == ":":
            key = value[:index].strip()
            if key:
                return key, value[index + 1 :].strip()
            return None
    return None


def _yaml_scalar(value: str) -> Any:
    value = _strip_yaml_comment(value).strip()
    if not value:
        return None
    if value[0:1] == value[-1:] and value[0:1] in {"'", '"'}:
        try:
            return ast.literal_eval(value)
        except (SyntaxError, ValueError) as error:
            raise LayoutError(f"invalid YAML string: {value!r}") from error
    lowered = value.lower()
    if lowered in {"null", "~"}:
        return None
    if lowered in {"true", "yes", "on"}:
        return True
    if lowered in {"false", "no", "off"}:
        return False
    if value.startswith("[") or value.startswith("{"):
        try:
            return json.loads(value)
        except json.JSONDecodeError as error:
            raise LayoutError(
                "the fallback YAML reader only accepts JSON-style flow "
                f"values, got {value!r}"
            ) from error
    if re.fullmatch(r"[-+]?0[xX][0-9a-fA-F]+", value):
        return int(value, 0)
    if re.fullmatch(r"[-+]?[0-9]+", value):
        return int(value, 10)
    return value


def _yaml_lines(text: str) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        if "\t" in raw_line[: len(raw_line) - len(raw_line.lstrip())]:
            raise LayoutError(f"YAML line {line_number}: tabs are not supported")
        stripped = _strip_yaml_comment(raw_line).strip()
        if not stripped or stripped == "---":
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        result.append((indent, stripped))
    return result


def _parse_simple_yaml(text: str) -> Any:
    """Read the subset needed by the layout files without PyYAML."""

    lines = _yaml_lines(text)

    def parse_block(index: int, indent: int) -> tuple[Any, int]:
        if index >= len(lines) or lines[index][0] != indent:
            raise LayoutError("invalid YAML indentation")
        is_list = lines[index][1].startswith("-")
        result: Any = [] if is_list else {}

        while index < len(lines) and lines[index][0] == indent:
            content = lines[index][1]
            if content.startswith("-") != is_list:
                break
            if is_list:
                rest = content[1:].strip()
                if not rest:
                    if index + 1 >= len(lines) or lines[index + 1][0] <= indent:
                        result.append(None)
                        index += 1
                        continue
                    item, index = parse_block(index + 1, lines[index + 1][0])
                    result.append(item)
                    continue

                pair = _split_yaml_key(rest)
                if pair is None:
                    result.append(_yaml_scalar(rest))
                    index += 1
                    continue

                key, raw_value = pair
                item: dict[str, Any] = {key: _yaml_scalar(raw_value)}
                index += 1
                if raw_value == "" and index < len(lines) and lines[index][0] > indent:
                    child, index = parse_block(index, lines[index][0])
                    item[key] = child

                if index < len(lines) and lines[index][0] > indent:
                    child_indent = lines[index][0]
                    if child_indent <= indent:
                        raise LayoutError("invalid YAML sequence indentation")
                    extra, index = parse_block(index, child_indent)
                    if not isinstance(extra, dict):
                        raise LayoutError("a YAML list item must contain a mapping")
                    item.update(extra)
                result.append(item)
                continue

            pair = _split_yaml_key(content)
            if pair is None:
                raise LayoutError(f"expected a YAML mapping entry, got {content!r}")
            key, raw_value = pair
            result[key] = _yaml_scalar(raw_value)
            index += 1
            if raw_value == "" and index < len(lines) and lines[index][0] > indent:
                child, index = parse_block(index, lines[index][0])
                result[key] = child
        return result, index

    if not lines:
        return None
    value, index = parse_block(0, lines[0][0])
    if index != len(lines):
        raise LayoutError("unexpected YAML content after the root value")
    return value


def load_document(path: Path, format_name: str | None) -> dict[str, Any]:
    selected = (format_name or path.suffix.lstrip(".")).lower()
    if selected not in SUPPORTED_FORMATS:
        raise LayoutError(
            f"unsupported input format {selected!r}; use JSON, TOML, or YAML"
        )
    text = path.read_text(encoding="utf-8")
    try:
        if selected == "json":
            value = json.loads(text)
        elif selected == "toml":
            try:
                import tomllib
            except ModuleNotFoundError:  # Python 3.10
                try:
                    import tomli as tomllib  # type: ignore[no-redef]
                except ModuleNotFoundError as error:
                    raise LayoutError(
                        "TOML input on Python 3.10 requires the 'tomli' package"
                    ) from error
            value = tomllib.loads(text)
        else:
            try:
                import yaml  # type: ignore[import-not-found]
            except ModuleNotFoundError:
                value = _parse_simple_yaml(text)
            else:
                value = yaml.safe_load(text)
    except (OSError, ValueError, TypeError) as error:
        raise LayoutError(f"could not parse {path}: {error}") from error
    if not isinstance(value, dict):
        raise LayoutError("the layout document root must be a mapping/object")
    return value


def _required(mapping: dict[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise LayoutError(f"{context} is missing required key {key!r}")
    return mapping[key]


def _as_string(value: Any, context: str) -> str:
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise LayoutError(f"{context} must be a string or integer")
    return str(value)


def parse_size(value: Any, context: str) -> int:
    text = _as_string(value, context).replace("_", "").strip()
    match = SIZE_RE.fullmatch(text)
    if not match:
        raise LayoutError(
            f"{context} must be an integer with optional K/M/G suffix, got {value!r}"
        )
    number = int(match.group("number"), 0)
    unit = (match.group("unit") or "").lower()
    multiplier = {"": 1, "k": 1024, "m": 1024**2, "g": 1024**3}[unit]
    return number * multiplier


def ld_number(value: Any, context: str) -> str:
    _ = parse_size(value, context)
    if isinstance(value, int) and not isinstance(value, bool):
        return f"0x{value:X}"
    return str(value)


def camel_identifier(value: str) -> str:
    words = [word for word in re.split(r"[^A-Za-z0-9]+", value) if word]
    result = "".join(word[0].upper() + word[1:] for word in words)
    if not result:
        raise LayoutError(f"cannot derive a C++ enum key from {value!r}")
    if result[0].isdigit():
        result = "N" + result
    return result


def validate_identifier(value: str, context: str) -> str:
    if not IDENTIFIER_RE.fullmatch(value):
        raise LayoutError(f"{context} must be a C/C++ identifier, got {value!r}")
    return value


def normalize_layout(document: dict[str, Any]) -> dict[str, Any]:
    if document.get("schema_version", 1) != 1:
        raise LayoutError("only schema_version: 1 is supported")

    header_guard = validate_identifier(
        str(document.get("header_guard", "UAI_AI_STATIC_MEMORY_LAYOUT_HPP")),
        "header_guard",
    )
    raw_header_guard = validate_identifier(
        str(
            document.get(
                "raw_header_guard", "UAI_AI_STATIC_MEMORY_LAYOUT_RAW_HPP"
            )
        ),
        "raw_header_guard",
    )
    cpp_namespace = _as_string(
        document.get("cpp_namespace", "uai::ai::static_memory_layout"),
        "cpp_namespace",
    )
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:]*", cpp_namespace):
        raise LayoutError(f"cpp_namespace is not a valid C++ namespace: {cpp_namespace!r}")
    symbol_prefix = validate_identifier(
        str(document.get("symbol_prefix", "sample_ai")), "symbol_prefix"
    )

    raw_memories = _required(document, "memory_regions", "layout document")
    raw_allocations = _required(document, "allocations", "layout document")
    if not isinstance(raw_memories, list) or not isinstance(raw_allocations, list):
        raise LayoutError("memory_regions and allocations must be arrays/lists")

    memories: list[dict[str, Any]] = []
    memory_names: set[str] = set()
    memory_ranges: list[tuple[int, int, str]] = []
    for index, raw in enumerate(raw_memories):
        context = f"memory_regions[{index}]"
        if not isinstance(raw, dict):
            raise LayoutError(f"{context} must be an object/mapping")
        name = validate_identifier(str(_required(raw, "name", context)), f"{context}.name")
        if name in memory_names:
            raise LayoutError(f"duplicate memory region {name!r}")
        memory_names.add(name)
        attributes = _as_string(raw.get("attributes", "rwx"), f"{context}.attributes")
        if not re.fullmatch(r"[rwx!]+", attributes):
            raise LayoutError(f"{context}.attributes contains invalid ld flags")
        origin = _required(raw, "origin", context)
        length = _required(raw, "length", context)
        origin_value = parse_size(origin, f"{context}.origin")
        length_value = parse_size(length, f"{context}.length")
        if length_value <= 0:
            raise LayoutError(f"{context}.length must be positive")
        memory_ranges.append((origin_value, origin_value + length_value, name))
        memories.append(
            {
                "name": name,
                "attributes": attributes,
                "origin": ld_number(origin, f"{context}.origin"),
                "length": ld_number(length, f"{context}.length"),
                "origin_value": origin_value,
                "length_value": length_value,
            }
        )

    for index, (start, end, name) in enumerate(memory_ranges):
        for other_start, other_end, other_name in memory_ranges[index + 1 :]:
            if start < other_end and other_start < end:
                raise LayoutError(f"memory regions {name!r} and {other_name!r} overlap")

    allocations: list[dict[str, Any]] = []
    allocation_names: set[str] = set()
    allocation_keys: set[str] = set()
    allocation_symbols: set[str] = set()
    used_by_memory: dict[str, int] = {memory["name"]: 0 for memory in memories}
    for index, raw in enumerate(raw_allocations):
        context = f"allocations[{index}]"
        if not isinstance(raw, dict):
            raise LayoutError(f"{context} must be an object/mapping")
        name = validate_identifier(str(_required(raw, "name", context)), f"{context}.name")
        memory = validate_identifier(
            str(_required(raw, "memory", context)), f"{context}.memory"
        )
        if memory not in memory_names:
            raise LayoutError(f"{context}.memory references unknown region {memory!r}")
        if name in allocation_names:
            raise LayoutError(f"duplicate allocation {name!r}")
        allocation_names.add(name)
        key = validate_identifier(
            str(raw.get("key", "k" + camel_identifier(name))),
            f"{context}.key",
        )
        if not key.startswith("k"):
            raise LayoutError(f"{context}.key must start with 'k'")
        if key in allocation_keys:
            raise LayoutError(f"duplicate C++ enum key {key!r}")
        allocation_keys.add(key)
        symbol = validate_identifier(
            str(raw.get("symbol", f"{symbol_prefix}_{name}")),
            f"{context}.symbol",
        )
        if symbol in allocation_symbols:
            raise LayoutError(f"duplicate linker symbol base {symbol!r}")
        allocation_symbols.add(symbol)
        section = validate_identifier(
            str(_required(raw, "section", context)), f"{context}.section"
        )
        size = _required(raw, "size", context)
        size_value = parse_size(size, f"{context}.size")
        if size_value <= 0:
            raise LayoutError(f"{context}.size must be positive")
        alignment = raw.get("alignment", 1)
        alignment_value = parse_size(alignment, f"{context}.alignment")
        if alignment_value <= 0 or alignment_value & (alignment_value - 1):
            raise LayoutError(f"{context}.alignment must be a power of two")
        noload = raw.get("noload", True)
        if not isinstance(noload, bool):
            raise LayoutError(f"{context}.noload must be boolean")
        current = used_by_memory[memory]
        current = (current + alignment_value - 1) // alignment_value * alignment_value
        used_by_memory[memory] = current + size_value
        allocations.append(
            {
                "name": name,
                "key": key,
                "symbol": symbol,
                "memory": memory,
                "section": section,
                "size": ld_number(size, f"{context}.size"),
                "size_value": size_value,
                "alignment": ld_number(alignment, f"{context}.alignment"),
                "alignment_value": alignment_value,
                "noload": noload,
            }
        )

    memory_by_name = {memory["name"]: memory for memory in memories}
    for memory_name, used in used_by_memory.items():
        if used > memory_by_name[memory_name]["length_value"]:
            raise LayoutError(
                f"fixed allocations in {memory_name} need at least {used:#x} bytes, "
                f"but the region is {memory_by_name[memory_name]['length_value']:#x} bytes"
            )

    return {
        "header_guard": header_guard,
        "raw_header_guard": raw_header_guard,
        "cpp_namespace": cpp_namespace,
        "symbol_prefix": symbol_prefix,
        "memories": memories,
        "allocations": allocations,
    }


def _indexed_region_keys(allocations: list[dict[str, Any]]) -> dict[str, list[str]]:
    indexed_keys: dict[str, list[str]] = {}
    for prefix in ("kInference", "kInferenceSource"):
        matches: list[tuple[int, str]] = []
        pattern = re.compile(rf"^{re.escape(prefix)}(?P<index>[0-9]+)$")
        for allocation in allocations:
            key = str(allocation["key"])
            match = pattern.fullmatch(key)
            if match:
                matches.append((int(match.group("index")), key))
        if not matches:
            raise LayoutError(f"layout is missing indexed {prefix} regions")
        indices = sorted(index for index, _ in matches)
        if indices != list(range(len(indices))):
            raise LayoutError(
                f"{prefix} region keys must be contiguous from index 0, got {indices}"
            )
        indexed_keys[prefix] = [key for _, key in sorted(matches)]
    return indexed_keys


def generate_key_header(layout: dict[str, Any]) -> str:
    allocations = layout["allocations"]
    indexed_keys = _indexed_region_keys(allocations)
    inference_keys = indexed_keys["kInference"]
    inference_source_keys = indexed_keys["kInferenceSource"]
    lines = [
        "/* Generated by tools/auto_static_memory_layout. Do not edit. */",
        "",
        "#ifndef UAI_AI_STATIC_MEMORY_LAYOUT_KEY_HPP",
        "#define UAI_AI_STATIC_MEMORY_LAYOUT_KEY_HPP",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        f"namespace {layout['cpp_namespace']} {{",
        "",
        "enum class Key : std::uint8_t {",
    ]
    for allocation in allocations:
        lines.append(f"    {allocation['key']},")
    lines.extend(
        [
            "    kCount,",
            "};",
            "",
            "inline constexpr std::array<Key, "
            f"{len(inference_keys)}> kInferenceRegionKeys = {{",
        ]
    )
    lines.extend(f"    Key::{key}," for key in inference_keys)
    lines.extend(
        [
            "};",
            "",
            "inline constexpr std::array<Key, "
            f"{len(inference_source_keys)}> kInferenceSourceRegionKeys = {{",
        ]
    )
    lines.extend(f"    Key::{key}," for key in inference_source_keys)
    lines.extend(
        [
            "};",
            "",
            f"}} // namespace {layout['cpp_namespace']}",
            "",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def generate_raw_header(layout: dict[str, Any]) -> str:
    guard = layout["raw_header_guard"]
    allocations = layout["allocations"]
    lines = [
        "/* Generated by tools/auto_static_memory_layout. Do not edit. */",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <cstdint>",
        "",
        '#include "middleware/memory/static_memory_layout.hpp"',
        '#include "middleware/memory/generated/static_memory_layout/key.hpp"',
        "",
        'extern "C" {',
    ]
    for allocation in allocations:
        symbol = allocation["symbol"]
        lines.extend(
            [
                f"extern std::uint8_t __{symbol}_start__[];",
                f"extern std::uint8_t __{symbol}_end__[];",
            ]
        )
    lines.extend(
        [
            "}",
            "",
            f"namespace {layout['cpp_namespace']} {{",
            "",
            "/* Addresses and capacities are emitted from one layout definition so",
            " * the allocator cannot silently diverge from the linker map. */",
            "inline constexpr Layout<static_cast<std::size_t>(Key::kCount)> kLayout = {",
            "    {{",
        ]
    )
    for allocation in allocations:
        symbol = allocation["symbol"]
        lines.append(f"        {{__{symbol}_start__, __{symbol}_end__}},")
    lines.extend(
        [
            "    }},",
            "};",
            "",
            f"}} // namespace {layout['cpp_namespace']}",
            "",
            f"#endif // {guard}",
            "",
        ]
    )
    return "\n".join(lines)


def generate_memory_regions(layout: dict[str, Any]) -> str:
    return "\n".join(
        f"  {memory['name']} ({memory['attributes']}) : ORIGIN = "
        f"{memory['origin']}, LENGTH = {memory['length']}"
        for memory in layout["memories"]
    )


def _group_allocations(allocations: Iterable[dict[str, Any]]) -> list[list[dict[str, Any]]]:
    groups: list[list[dict[str, Any]]] = []
    group_keys: list[tuple[str, str, bool]] = []
    for allocation in allocations:
        key = (allocation["section"], allocation["memory"], allocation["noload"])
        if key not in group_keys:
            group_keys.append(key)
            groups.append([])
        groups[group_keys.index(key)].append(allocation)
    return groups


def generate_static_sections(layout: dict[str, Any]) -> str:
    blocks: list[str] = []
    for group in _group_allocations(layout["allocations"]):
        first = group[0]
        output_section = "." + first["section"]
        attributes = " (NOLOAD)" if first["noload"] else ""
        lines = [f"  {output_section}{attributes} :", "  {"]
        for allocation in group:
            lines.extend(
                [
                    f"    . = ALIGN({allocation['alignment']});",
                    f"    __{allocation['symbol']}_start__ = .;",
                    f"    . = . + {allocation['size']};",
                    f"    __{allocation['symbol']}_end__ = .;",
                ]
            )
        lines.extend([f"  }} >{first['memory']}", ""])
        blocks.append("\n".join(lines))
    return "\n".join(blocks).rstrip() + "\n"


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")
