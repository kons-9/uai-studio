"""Layout document model and validation for on-screen UI definitions.

The document is JSON (``ui_layout.json``). It is the single source of truth:
the browser editor edits it, ``render`` previews it, and ``generate`` turns it
into a C++ header consumed by ``kernel/middleware/ui``.

Widget kinds:
  button  tappable; ``label`` text, ButtonStyle, optional on_tap/on_press
  label   text the firmware replaces at run time; ``text`` is the initial value
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .font import supported_characters

SCHEMA_VERSION = 1
IDENTIFIER_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CPP_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

WIDGET_TYPES = ("button", "label")
# Event names map to ui::EventType members and to the per-widget callback keys.
CALLBACK_EVENTS: tuple[str, ...] = ("on_tap", "on_press")
GEOMETRY_KEYS = ("x", "y", "width", "height")
WIDGET_KEYS = frozenset(
    ("type", "id", "label", "text", "style") + GEOMETRY_KEYS + CALLBACK_EVENTS)
ALIGNMENTS = ("left", "center", "right")

# Keep these defaults equal to ui::ButtonStyle / ui::LabelStyle in widget.hpp.
BUTTON_STYLE_DEFAULTS: dict[str, Any] = {
    "fill": "#2060C0",
    "pressed_fill": "#103060",
    "checked_fill": "#00A060",
    "border": "#FFFFFF",
    "text": "#FFFFFF",
    "text_scale": 3,
    "border_width": 2,
}
LABEL_STYLE_DEFAULTS: dict[str, Any] = {
    "text": "#FFFFFF",
    "fill": "#000000",      # null means transparent
    "text_scale": 2,
    "align": "left",
    "padding": 4,
}
# Mirrors ui::kLabelTextCapacity; longer initial text would be truncated.
LABEL_TEXT_CAPACITY = 64


class LayoutError(ValueError):
    """Raised for structurally invalid layout documents."""


@dataclass
class ButtonStyle:
    fill: str = BUTTON_STYLE_DEFAULTS["fill"]
    pressed_fill: str = BUTTON_STYLE_DEFAULTS["pressed_fill"]
    checked_fill: str = BUTTON_STYLE_DEFAULTS["checked_fill"]
    border: str = BUTTON_STYLE_DEFAULTS["border"]
    text: str = BUTTON_STYLE_DEFAULTS["text"]
    text_scale: int = BUTTON_STYLE_DEFAULTS["text_scale"]
    border_width: int = BUTTON_STYLE_DEFAULTS["border_width"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class LabelStyle:
    text: str = LABEL_STYLE_DEFAULTS["text"]
    fill: str | None = LABEL_STYLE_DEFAULTS["fill"]
    text_scale: int = LABEL_STYLE_DEFAULTS["text_scale"]
    align: str = LABEL_STYLE_DEFAULTS["align"]
    padding: int = LABEL_STYLE_DEFAULTS["padding"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class Widget:
    type: str
    id: str
    text: str
    x: int
    y: int
    width: int
    height: int
    style: ButtonStyle | LabelStyle = field(default_factory=ButtonStyle)
    # C++ method names invoked by the generated Dispatch(); empty means none.
    on_tap: str = ""
    on_press: str = ""

    @property
    def is_button(self) -> bool:
        return self.type == "button"

    def to_document(self) -> dict[str, Any]:
        entry: dict[str, Any] = {
            "type": self.type,
            "id": self.id,
            "label" if self.is_button else "text": self.text,
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
            "style": self.style.to_document(),
        }
        for event in CALLBACK_EVENTS:
            name = getattr(self, event)
            if name:
                entry[event] = name
        return entry


# Backwards-compatible alias used by older callers/tests.
Button = Widget


@dataclass
class Layout:
    width: int
    height: int
    namespace: str
    widgets: list[Widget]

    def buttons(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_button]

    def labels(self) -> list[Widget]:
        return [w for w in self.widgets if not w.is_button]

    def to_document(self) -> dict[str, Any]:
        return {
            "schema_version": SCHEMA_VERSION,
            "screen": {"width": self.width, "height": self.height},
            "namespace": self.namespace,
            "widgets": [widget.to_document() for widget in self.widgets],
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


def _style_mapping(value: Any, defaults: dict[str, Any], context: str) -> dict[str, Any]:
    if value is None:
        return dict(defaults)
    if not isinstance(value, dict):
        raise LayoutError(f"{context} must be an object")
    unknown = set(value) - set(defaults)
    if unknown:
        raise LayoutError(f"{context} has unknown keys {sorted(unknown)}")
    return {**defaults, **value}


def _button_style(value: Any, context: str) -> ButtonStyle:
    merged = _style_mapping(value, BUTTON_STYLE_DEFAULTS, context)
    return ButtonStyle(
        fill=_color(merged["fill"], f"{context}.fill"),
        pressed_fill=_color(merged["pressed_fill"], f"{context}.pressed_fill"),
        checked_fill=_color(merged["checked_fill"], f"{context}.checked_fill"),
        border=_color(merged["border"], f"{context}.border"),
        text=_color(merged["text"], f"{context}.text"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        border_width=_int(merged["border_width"], f"{context}.border_width", 0, 32),
    )


def _label_style(value: Any, context: str) -> LabelStyle:
    merged = _style_mapping(value, LABEL_STYLE_DEFAULTS, context)
    align = merged["align"]
    if align not in ALIGNMENTS:
        raise LayoutError(f"{context}.align must be one of {ALIGNMENTS}, got {align!r}")
    return LabelStyle(
        text=_color(merged["text"], f"{context}.text"),
        fill=None if merged["fill"] is None else _color(merged["fill"], f"{context}.fill"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        align=align,
        padding=_int(merged["padding"], f"{context}.padding", 0, 64),
    )


def _callback(value: Any, context: str) -> str:
    if value is None or value == "":
        return ""
    if not isinstance(value, str) or not CPP_IDENTIFIER_RE.match(value):
        raise LayoutError(f"{context} must be a C++ identifier, got {value!r}")
    return value


def _text(raw: dict[str, Any], key: str, context: str, allowed: frozenset[str]) -> str:
    text = raw.get(key, "")
    if not isinstance(text, str):
        raise LayoutError(f"{context}.{key} must be a string")
    bad = sorted({c for c in text if c.upper() not in allowed})
    if bad:
        raise LayoutError(f"{context}.{key} contains characters without glyphs: {bad}")
    return text


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
    widgets: list[Widget] = []
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
        if widget_type not in WIDGET_TYPES:
            raise LayoutError(f"{context}.type {widget_type!r} is not one of {WIDGET_TYPES}")
        widget_id = raw.get("id")
        if not isinstance(widget_id, str) or not IDENTIFIER_RE.match(widget_id):
            raise LayoutError(
                f"{context}.id must match [a-z][a-z0-9_]*, got {widget_id!r}")
        if widget_id in seen_ids:
            raise LayoutError(f"{context}.id {widget_id!r} is duplicated")
        seen_ids.add(widget_id)
        x = _int(raw.get("x"), f"{context}.x", 0, width - 1)
        y = _int(raw.get("y"), f"{context}.y", 0, height - 1)
        w = _int(raw.get("width"), f"{context}.width", 1, width - x)
        h = _int(raw.get("height"), f"{context}.height", 1, height - y)

        if widget_type == "button":
            if "text" in raw:
                raise LayoutError(f"{context}: buttons use 'label', not 'text'")
            widgets.append(Widget(
                type="button", id=widget_id,
                text=_text(raw, "label", context, allowed),
                x=x, y=y, width=w, height=h,
                style=_button_style(raw.get("style"), f"{context}.style"),
                on_tap=_callback(raw.get("on_tap"), f"{context}.on_tap"),
                on_press=_callback(raw.get("on_press"), f"{context}.on_press")))
        else:
            for key in ("label",) + CALLBACK_EVENTS:
                if key in raw:
                    raise LayoutError(f"{context}: labels do not support {key!r}")
            text = _text(raw, "text", context, allowed)
            if len(text) >= LABEL_TEXT_CAPACITY:
                raise LayoutError(
                    f"{context}.text must be shorter than {LABEL_TEXT_CAPACITY} characters")
            widgets.append(Widget(
                type="label", id=widget_id, text=text,
                x=x, y=y, width=w, height=h,
                style=_label_style(raw.get("style"), f"{context}.style")))

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
