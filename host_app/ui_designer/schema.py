"""Layout document model and validation for on-screen UI definitions.

The document is JSON (``ui_layout.json``). It is the single source of truth:
the browser editor edits it, ``render`` previews it, and ``generate`` turns it
into a C++ header consumed by ``kernel/middleware/ui``.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any

from .font import supported_characters

SCHEMA_VERSION = 1
IDENTIFIER_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CPP_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

# Event names map to ui::EventType members and to the per-widget callback keys.
CALLBACK_EVENTS: tuple[str, ...] = ("on_tap", "on_press")
WIDGET_KEYS = frozenset(
    ("type", "id", "label", "x", "y", "width", "height", "style") + CALLBACK_EVENTS)

DEFAULT_STYLE: dict[str, Any] = {
    "fill": "#2060C0",
    "pressed_fill": "#103060",
    "border": "#FFFFFF",
    "text": "#FFFFFF",
    "text_scale": 3,
    "border_width": 2,
}


class LayoutError(ValueError):
    """Raised for structurally invalid layout documents."""


@dataclass
class Style:
    fill: str = DEFAULT_STYLE["fill"]
    pressed_fill: str = DEFAULT_STYLE["pressed_fill"]
    border: str = DEFAULT_STYLE["border"]
    text: str = DEFAULT_STYLE["text"]
    text_scale: int = DEFAULT_STYLE["text_scale"]
    border_width: int = DEFAULT_STYLE["border_width"]


@dataclass
class Button:
    id: str
    label: str
    x: int
    y: int
    width: int
    height: int
    style: Style = field(default_factory=Style)
    # C++ method names invoked by the generated Dispatch(); empty means none.
    on_tap: str = ""
    on_press: str = ""
    type: str = "button"


@dataclass
class Layout:
    width: int
    height: int
    namespace: str
    widgets: list[Button]

    def to_document(self) -> dict[str, Any]:
        widgets = []
        for widget in self.widgets:
            entry = {
                "type": widget.type,
                "id": widget.id,
                "label": widget.label,
                "x": widget.x,
                "y": widget.y,
                "width": widget.width,
                "height": widget.height,
                "style": asdict(widget.style),
            }
            for event in CALLBACK_EVENTS:
                name = getattr(widget, event)
                if name:
                    entry[event] = name
            widgets.append(entry)
        return {
            "schema_version": SCHEMA_VERSION,
            "screen": {"width": self.width, "height": self.height},
            "namespace": self.namespace,
            "widgets": widgets,
        }


def _int(value: Any, context: str, minimum: int = 0, maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise LayoutError(f"{context} must be an integer, got {value!r}")
    if value < minimum or (maximum is not None and value > maximum):
        upper = "" if maximum is None else f" and <= {maximum}"
        raise LayoutError(f"{context} must be >= {minimum}{upper}, got {value}")
    return value


def _color(value: Any, context: str) -> str:
    if not isinstance(value, str) or not COLOR_RE.match(value):
        raise LayoutError(f"{context} must be a #RRGGBB color, got {value!r}")
    return value.upper()


def parse_color(color: str) -> tuple[int, int, int]:
    return int(color[1:3], 16), int(color[3:5], 16), int(color[5:7], 16)


def rgb565(color: str) -> int:
    red, green, blue = parse_color(color)
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def _style(value: Any, context: str) -> Style:
    if value is None:
        return Style()
    if not isinstance(value, dict):
        raise LayoutError(f"{context} must be an object")
    unknown = set(value) - set(DEFAULT_STYLE)
    if unknown:
        raise LayoutError(f"{context} has unknown keys {sorted(unknown)}")
    merged = {**DEFAULT_STYLE, **value}
    return Style(
        fill=_color(merged["fill"], f"{context}.fill"),
        pressed_fill=_color(merged["pressed_fill"], f"{context}.pressed_fill"),
        border=_color(merged["border"], f"{context}.border"),
        text=_color(merged["text"], f"{context}.text"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        border_width=_int(merged["border_width"], f"{context}.border_width", 0, 32),
    )


def _callback(value: Any, context: str) -> str:
    if value is None or value == "":
        return ""
    if not isinstance(value, str) or not CPP_IDENTIFIER_RE.match(value):
        raise LayoutError(f"{context} must be a C++ identifier, got {value!r}")
    return value


def parse_layout(document: Any) -> Layout:
    if not isinstance(document, dict):
        raise LayoutError("layout root must be an object")
    version = document.get("schema_version", SCHEMA_VERSION)
    if version != SCHEMA_VERSION:
        raise LayoutError(f"unsupported schema_version {version!r}")
    screen = document.get("screen")
    if not isinstance(screen, dict):
        raise LayoutError("screen must be an object with width and height")
    width = _int(screen.get("width"), "screen.width", 1, 4096)
    height = _int(screen.get("height"), "screen.height", 1, 4096)
    namespace = document.get("namespace", "uai::ai::app_ui")
    if not isinstance(namespace, str) or not NAMESPACE_RE.match(namespace):
        raise LayoutError(f"namespace must be a C++ qualified name, got {namespace!r}")

    widgets_raw = document.get("widgets", [])
    if not isinstance(widgets_raw, list):
        raise LayoutError("widgets must be a list")
    widgets: list[Button] = []
    seen_ids: set[str] = set()
    allowed = supported_characters()
    for index, raw in enumerate(widgets_raw):
        context = f"widgets[{index}]"
        if not isinstance(raw, dict):
            raise LayoutError(f"{context} must be an object")
        unknown = set(raw) - WIDGET_KEYS
        if unknown:
            raise LayoutError(f"{context} has unknown keys {sorted(unknown)}")
        widget_type = raw.get("type", "button")
        if widget_type != "button":
            raise LayoutError(f"{context}.type {widget_type!r} is not supported")
        widget_id = raw.get("id")
        if not isinstance(widget_id, str) or not IDENTIFIER_RE.match(widget_id):
            raise LayoutError(
                f"{context}.id must match [a-z][a-z0-9_]*, got {widget_id!r}")
        if widget_id in seen_ids:
            raise LayoutError(f"{context}.id {widget_id!r} is duplicated")
        seen_ids.add(widget_id)
        label = raw.get("label", "")
        if not isinstance(label, str):
            raise LayoutError(f"{context}.label must be a string")
        bad = sorted({c for c in label if c.upper() not in allowed})
        if bad:
            raise LayoutError(
                f"{context}.label contains characters without glyphs: {bad}")
        x = _int(raw.get("x"), f"{context}.x", 0, width - 1)
        y = _int(raw.get("y"), f"{context}.y", 0, height - 1)
        w = _int(raw.get("width"), f"{context}.width", 1, width - x)
        h = _int(raw.get("height"), f"{context}.height", 1, height - y)
        widgets.append(Button(
            id=widget_id, label=label, x=x, y=y, width=w, height=h,
            style=_style(raw.get("style"), f"{context}.style"),
            on_tap=_callback(raw.get("on_tap"), f"{context}.on_tap"),
            on_press=_callback(raw.get("on_press"), f"{context}.on_press")))

    for i, a in enumerate(widgets):
        for b in widgets[i + 1:]:
            if (a.x < b.x + b.width and b.x < a.x + a.width and
                    a.y < b.y + b.height and b.y < a.y + a.height):
                raise LayoutError(f"widgets {a.id!r} and {b.id!r} overlap")
    return Layout(width=width, height=height, namespace=namespace, widgets=widgets)


def load_layout(path: Path) -> Layout:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise LayoutError(f"could not read {path}: {error}") from error
    return parse_layout(document)


def dump_layout(layout: Layout) -> str:
    return json.dumps(layout.to_document(), indent=2) + "\n"


def save_layout(path: Path, layout: Layout) -> None:
    path.write_text(dump_layout(layout), encoding="utf-8")


def pascal_case(identifier: str) -> str:
    return "".join(part.capitalize() for part in identifier.split("_") if part)
