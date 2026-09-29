#!/usr/bin/env python3
"""Measure model binaries and plan collision-free external NOR placement.

This tool never changes STAI-generated addresses, linker options or flash.
A placement without an address is a *proposal* and must be regenerated with
STEdgeAI before it can be programmed on a board.
"""

import argparse
import json
from pathlib import Path
import sys
from xml.sax.saxutils import escape


class LayoutError(ValueError):
    """Invalid or unplaceable layout."""


def number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise LayoutError(f"{label}: expected an integer or 0x... string")
    try:
        result = int(value, 0) if isinstance(value, str) else value
    except ValueError as exc:
        raise LayoutError(f"{label}: invalid number {value!r}") from exc
    if result < 0:
        raise LayoutError(f"{label}: must be non-negative")
    return result


def alignment(value, label):
    result = number(value, label)
    if result == 0 or result & (result - 1):
        raise LayoutError(f"{label}: alignment must be a power of two")
    return result


def align_up(value, step):
    return (value + step - 1) // step * step


def check_entries(entries, start, end):
    """Validate half-open erase intervals, including pinned binary overlaps."""
    ordered = sorted(entries, key=lambda item: (item["address"], item["name"]))
    previous = None
    for item in ordered:
        if item["address"] < start or item["end"] > end:
            raise LayoutError(
                f"{item['name']}: 0x{item['address']:X}..0x{item['end']:X} "
                f"is outside region 0x{start:X}..0x{end:X}")
        if previous and item["address"] < previous["end"]:
            raise LayoutError(f"{previous['name']} overlaps {item['name']} "
                              "(erase-aligned intervals)")
        previous = item
    return ordered


def free_spans(entries, start, end):
    cursor = start
    for item in sorted(entries, key=lambda x: x["address"]):
        if cursor < item["address"]:
            yield cursor, item["address"]
        cursor = item["end"]
    if cursor < end:
        yield cursor, end


def plan(manifest, manifest_path):
    if not isinstance(manifest, dict):
        raise LayoutError("manifest must be a JSON object")
    region = manifest.get("region")
    if not isinstance(region, dict):
        raise LayoutError("region must be an object")
    start = number(region.get("start"), "region.start")
    size = number(region.get("size"), "region.size")
    erase_size = alignment(region.get("erase_size", 4096), "region.erase_size")
    default_alignment = alignment(region.get("alignment", erase_size),
                                  "region.alignment")
    if size == 0 or start % erase_size or size % erase_size:
        raise LayoutError("region must have a nonzero, erase-aligned size/start")
    end = start + size
    raw_artifacts = manifest.get("artifacts")
    raw_reserved = manifest.get("reserved", [])
    if not isinstance(raw_artifacts, list) or not raw_artifacts:
        raise LayoutError("artifacts must be a nonempty array")
    if not isinstance(raw_reserved, list):
        raise LayoutError("reserved must be an array")

    entries = []
    names = set()

    def unique_name(raw, field):
        name = raw.get("name")
        if not isinstance(name, str) or not name.strip():
            raise LayoutError(f"{field}.name must be a nonempty string")
        if name in names:
            raise LayoutError(f"duplicate name: {name}")
        names.add(name)
        return name

    for raw in raw_reserved:
        if not isinstance(raw, dict):
            raise LayoutError("reserved item must be an object")
        name = unique_name(raw, "reserved")
        address = number(raw.get("address"), f"reserved.{name}.address")
        reserved_size = number(raw.get("size"), f"reserved.{name}.size")
        if reserved_size == 0 or address % erase_size:
            raise LayoutError(f"reserved.{name}: empty or unaligned erase region")
        entries.append({"name": name, "kind": "reserved", "address": address,
                        "size": reserved_size,
                        "end": address + align_up(reserved_size, erase_size)})

    pending = []
    for raw in raw_artifacts:
        if not isinstance(raw, dict):
            raise LayoutError("artifact item must be an object")
        name = unique_name(raw, "artifact")
        source = raw.get("path")
        if not isinstance(source, str) or not source:
            raise LayoutError(f"artifact.{name}.path is required")
        path = Path(source).expanduser()
        if not path.is_absolute():
            path = manifest_path.parent / path
        if not path.is_file():
            raise LayoutError(f"artifact.{name}: binary not found: {path}")
        file_size = path.stat().st_size
        if file_size == 0:
            raise LayoutError(f"artifact.{name}: binary is empty")
        item_alignment = alignment(raw.get("alignment", default_alignment),
                                   f"artifact.{name}.alignment")
        # The erase-aligned interval avoids collateral erasure of an adjacent
        # payload when the external flash loader updates only one artifact.
        item_alignment = max(erase_size, item_alignment)
        item = {"name": name, "kind": "artifact", "path": str(path.resolve()),
                "size": file_size, "allocated_size": align_up(file_size, erase_size),
                "alignment": item_alignment, "fixed": "address" in raw}
        if item["fixed"]:
            address = number(raw["address"], f"artifact.{name}.address")
            if address % item_alignment:
                raise LayoutError(f"artifact.{name}: address is not aligned "
                                  f"to {item_alignment} bytes")
            item.update(address=address, end=address + item["allocated_size"])
            entries.append(item)
        else:
            pending.append(item)

    check_entries(entries, start, end)
    # Deterministic decreasing first-fit: a packing heuristic, not a proof
    # of optimality. Pinned addresses are never moved.
    for item in sorted(pending, key=lambda x: (-x["allocated_size"], x["name"])):
        for low, high in free_spans(entries, start, end):
            address = align_up(low, item["alignment"])
            if address + item["allocated_size"] <= high:
                item.update(address=address,
                            end=address + item["allocated_size"])
                entries.append(item)
                break
        else:
            raise LayoutError(f"{item['name']}: no contiguous erase-aligned "
                              f"space for {item['allocated_size']} bytes")
    ordered = check_entries(entries, start, end)
    free = [{"address": low, "end": high, "size": high - low}
            for low, high in free_spans(ordered, start, end)]
    used = sum(item["end"] - item["address"] for item in ordered)
    return {"region": {"name": region.get("name", "NOR model region"),
                       "start": start, "end": end, "size": size,
                       "erase_size": erase_size},
            "placements": ordered, "free": free,
            "summary": {"occupied_bytes": used, "free_bytes": size - used,
                        "payload_bytes": sum(x["size"] for x in ordered
                                             if x["kind"] == "artifact")}}


def draw_svg(result):
    """Standalone, no-dependency lane diagram (one lane per placement)."""
    region = result["region"]
    placements = result["placements"]
    width, left, scale_width = 1240, 170, 740
    height = 135 + 45 * len(placements)
    def x(address):
        return left + (address - region["start"]) * scale_width / region["size"]

    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
             f'height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#0d1422"/>',
             '<g font-family="sans-serif" fill="#e2e8f0">',
             f'<text x="24" y="34" font-size="20">{escape(str(region["name"]))}</text>',
             f'<text x="{left}" y="65" font-size="12">0x{region["start"]:08X}</text>',
             f'<text x="{left + scale_width - 90}" y="65" font-size="12">'
             f'0x{region["end"]:08X}</text>',
             f'<rect x="{left}" y="79" width="{scale_width}" height="22" '
             'rx="4" fill="#273449"/>']
    for index, item in enumerate(placements):
        y = 112 + 45 * index
        color = "#718096" if item["kind"] == "reserved" else (
            "#29b6a7" if item.get("fixed") else "#7e9df5")
        bar_width = max(2, x(item["end"]) - x(item["address"]))
        label = escape(item["name"])
        lines += [f'<text x="24" y="{y + 16}" font-size="13">{label}</text>',
                  f'<rect x="{left}" y="{y}" width="{scale_width}" height="23" '
                  'rx="3" fill="#1d2839"/>',
                  f'<rect x="{x(item["address"]):.2f}" y="{y}" '
                  f'width="{bar_width:.2f}" height="23" rx="3" fill="{color}"/>',
                  f'<text x="{left + scale_width + 12}" y="{y + 16}" '
                  f'font-size="12">0x{item["address"]:08X} '
                  f'({item["size"]:,} B)</text>']
    return "\n".join(lines + ["</g>", "</svg>", ""])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="JSON region and binary list")
    parser.add_argument("--json", type=Path, dest="json_output", help="report JSON")
    parser.add_argument("--svg", type=Path, help="memory map SVG")
    args = parser.parse_args(argv)
    try:
        result = plan(json.loads(args.manifest.read_text(encoding="utf-8")),
                      args.manifest.resolve())
        if args.json_output:
            args.json_output.write_text(json.dumps(result, indent=2) + "\n",
                                        encoding="utf-8")
        if args.svg:
            args.svg.write_text(draw_svg(result), encoding="utf-8")
    except (LayoutError, OSError, json.JSONDecodeError) as exc:
        parser.exit(2, f"layout error: {exc}\n")
    for item in result["placements"]:
        mode = ("reserved" if item["kind"] == "reserved" else
                "fixed" if item["fixed"] else "proposed")
        print(f"{item['name']:24} 0x{item['address']:08X}..0x{item['end']:08X} "
              f"{item['size']:>9} bytes  {mode}")
    print(f"free: {result['summary']['free_bytes']:,} bytes "
          "(proposed addresses require STAI regeneration and linker updates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
