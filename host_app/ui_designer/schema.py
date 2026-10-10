"""Layout document model and validation for on-screen UI definitions.

The document is JSON (``ui_layout.json``). It is the single source of truth:
the browser editor edits it, ``render`` previews it, and ``generate`` turns it
into a C++ header consumed by ``kernel/middleware/ui``.

Document shape (schema_version 2)::

    {"screen": {...}, "namespace": "...",
     "screens": [{"id": "main", "background": "camera" | "#RRGGBB",
                  "widgets": [...]}, ...]}

Widget kinds:
  button  tappable; ``label`` or ``icon``, ``shape`` (rectangle, rounded, pill,
          ellipse, triangle_*, diamond), ButtonStyle, on_tap/on_press,
          optional ``navigate`` to a screen id
  label   text the firmware replaces at run time; ``text`` is the initial value
  slider  horizontal value control; min/max/step/value, on_change
  dial    rotary value control (270-degree arc); min/max/step/value, on_change
  wheel   vertical item picker; ``items``, ``value`` (index), on_change
  number  numeric read-out set by the firmware; ``unit``, ``decimals``, ``value``
  image   RGB565 bitmap generated from ``source`` (PNG, relative to the layout
          file), resampled to the bounds; optional ``transparent`` key color
  pad     round four-way pad with optional ``center`` button; on_tap/on_press
          carry the segment (0 up, 1 right, 2 down, 3 left, 4 centre),
      on_change carries signed 45-degree steps (positive clockwise)

A schema_version 1 document (top-level ``widgets``) is read as a single
``main`` screen with the camera background.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .font import supported_characters

SCHEMA_VERSION = 2
IDENTIFIER_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CPP_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

WIDGET_TYPES = ("button", "label", "slider", "dial", "wheel", "number", "image", "pad")
# Event names map to ui::EventType members and to the per-widget callback keys.
CALLBACK_EVENTS: tuple[str, ...] = ("on_tap", "on_press", "on_change")
BUTTON_EVENTS: tuple[str, ...] = ("on_tap", "on_press")
SLIDER_EVENTS: tuple[str, ...] = ("on_change",)
VALUE_WIDGETS = ("slider", "dial")      # share min/max/step/value
GEOMETRY_KEYS = ("x", "y", "width", "height")
SLIDER_RANGE_KEYS = ("min", "max", "step", "value")
WIDGET_KEYS = frozenset(
    ("type", "id", "label", "text", "style", "icon", "shape", "navigate",
    "items", "unit", "decimals", "source", "transparent", "center", "feature", "operation")
    + GEOMETRY_KEYS + CALLBACK_EVENTS + SLIDER_RANGE_KEYS)
SCREEN_KEYS = frozenset(("id", "background", "widgets"))
ALIGNMENTS = ("left", "center", "right")
ICONS = ("none", "menu", "back", "close")
# Mirrors ui::Shape in canvas.hpp.
SHAPES = ("rectangle", "rounded", "pill", "ellipse", "triangle_up", "triangle_down",
          "triangle_left", "triangle_right", "diamond")
PAD_SEGMENTS = ("up", "right", "down", "left", "center")
PAD_MIN_SIDE = 32
MAX_PADS = 2
CAMERA_BACKGROUND = "camera"
DEFAULT_TRANSPARENT_KEY = "#FF00FF"
# Bitmaps live in the firmware image (RAM on the N6); keep them small.
IMAGE_MAX_BYTES = 128 * 1024
MAX_WHEEL_ITEMS = 32

# Keep these defaults equal to ui::*Style in widget.hpp.
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
SLIDER_STYLE_DEFAULTS: dict[str, Any] = {
    "track": "#404040",
    "fill": "#2060C0",
    "knob": "#FFFFFF",
    "text": "#FFFFFF",
    "text_scale": 2,
    "show_value": True,
}
DIAL_STYLE_DEFAULTS: dict[str, Any] = {
    "face": "#202830",
    "track": "#404040",
    "fill": "#2060C0",
    "pointer": "#FFFFFF",
    "text": "#FFFFFF",
    "text_scale": 2,
    "show_value": True,
}
WHEEL_STYLE_DEFAULTS: dict[str, Any] = {
    "fill": "#182028",
    "highlight": "#2060C0",
    "text": "#8090A0",
    "selected_text": "#FFFFFF",
    "border": "#FFFFFF",
    "text_scale": 2,
}
NUMBER_STYLE_DEFAULTS: dict[str, Any] = {
    "text": "#FFFFFF",
    "fill": "#000000",      # null means transparent
    "text_scale": 4,
    "align": "right",
}
PAD_STYLE_DEFAULTS: dict[str, Any] = {
    "fill": "#303030",
    "pressed_fill": "#606060",
    "center_fill": "#2060C0",
    "border": "#FFFFFF",
    "arrow": "#FFFFFF",
    "border_width": 2,
}
STYLE_DEFAULTS: dict[str, dict[str, Any]] = {
    "button": BUTTON_STYLE_DEFAULTS,
    "label": LABEL_STYLE_DEFAULTS,
    "slider": SLIDER_STYLE_DEFAULTS,
    "dial": DIAL_STYLE_DEFAULTS,
    "wheel": WHEEL_STYLE_DEFAULTS,
    "number": NUMBER_STYLE_DEFAULTS,
    "image": {},
    "pad": PAD_STYLE_DEFAULTS,
}
# Mirrors ui::kLabelTextCapacity; longer initial text would be truncated.
LABEL_TEXT_CAPACITY = 64
# Mirrors kernel/middleware/ui/widget.cpp slider geometry.
SLIDER_TRACK_HEIGHT = 8
SLIDER_KNOB_WIDTH = 16
SLIDER_KNOB_MARGIN = 6


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
class SliderStyle:
    track: str = SLIDER_STYLE_DEFAULTS["track"]
    fill: str = SLIDER_STYLE_DEFAULTS["fill"]
    knob: str = SLIDER_STYLE_DEFAULTS["knob"]
    text: str = SLIDER_STYLE_DEFAULTS["text"]
    text_scale: int = SLIDER_STYLE_DEFAULTS["text_scale"]
    show_value: bool = SLIDER_STYLE_DEFAULTS["show_value"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class DialStyle:
    face: str = DIAL_STYLE_DEFAULTS["face"]
    track: str = DIAL_STYLE_DEFAULTS["track"]
    fill: str = DIAL_STYLE_DEFAULTS["fill"]
    pointer: str = DIAL_STYLE_DEFAULTS["pointer"]
    text: str = DIAL_STYLE_DEFAULTS["text"]
    text_scale: int = DIAL_STYLE_DEFAULTS["text_scale"]
    show_value: bool = DIAL_STYLE_DEFAULTS["show_value"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class WheelStyle:
    fill: str = WHEEL_STYLE_DEFAULTS["fill"]
    highlight: str = WHEEL_STYLE_DEFAULTS["highlight"]
    text: str = WHEEL_STYLE_DEFAULTS["text"]
    selected_text: str = WHEEL_STYLE_DEFAULTS["selected_text"]
    border: str = WHEEL_STYLE_DEFAULTS["border"]
    text_scale: int = WHEEL_STYLE_DEFAULTS["text_scale"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class NumberStyle:
    text: str = NUMBER_STYLE_DEFAULTS["text"]
    fill: str | None = NUMBER_STYLE_DEFAULTS["fill"]
    text_scale: int = NUMBER_STYLE_DEFAULTS["text_scale"]
    align: str = NUMBER_STYLE_DEFAULTS["align"]

    def to_document(self) -> dict[str, Any]:
        return dict(vars(self))


@dataclass
class ImageStyle:
    def to_document(self) -> dict[str, Any]:
        return {}


@dataclass
class PadStyle:
    fill: str = PAD_STYLE_DEFAULTS["fill"]
    pressed_fill: str = PAD_STYLE_DEFAULTS["pressed_fill"]
    center_fill: str = PAD_STYLE_DEFAULTS["center_fill"]
    border: str = PAD_STYLE_DEFAULTS["border"]
    arrow: str = PAD_STYLE_DEFAULTS["arrow"]
    border_width: int = PAD_STYLE_DEFAULTS["border_width"]

    def to_document(self) -> dict[str, Any]:
        return dict(self.__dict__)


Style = (ButtonStyle | LabelStyle | SliderStyle | DialStyle | WheelStyle | NumberStyle
         | ImageStyle | PadStyle)


@dataclass
class Widget:
    type: str
    id: str
    text: str
    x: int
    y: int
    width: int
    height: int
    style: Style = field(default_factory=ButtonStyle)
    # C++ method names invoked by the generated Dispatch(); empty means none.
    on_tap: str = ""
    on_press: str = ""
    on_change: str = ""
    feature: str = ""
    operation: str = ""
    icon: str = "none"
    shape: str = "rectangle"
    navigate: str = ""
    minimum: int = 0
    maximum: int = 100
    step: int = 1
    value: int = 0
    items: list[str] = field(default_factory=list)
    unit: str = ""
    decimals: int = 0
    source: str = ""
    transparent: str | None = None
    center: bool = True

    @property
    def is_button(self) -> bool:
        return self.type == "button"

    @property
    def is_label(self) -> bool:
        return self.type == "label"

    @property
    def is_slider(self) -> bool:
        return self.type == "slider"

    @property
    def is_dial(self) -> bool:
        return self.type == "dial"

    @property
    def is_wheel(self) -> bool:
        return self.type == "wheel"

    @property
    def is_number(self) -> bool:
        return self.type == "number"

    @property
    def is_image(self) -> bool:
        return self.type == "image"

    @property
    def is_pad(self) -> bool:
        return self.type == "pad"

    def to_document(self) -> dict[str, Any]:
        entry: dict[str, Any] = {"type": self.type, "id": self.id}
        if self.is_label:
            entry["text"] = self.text
        elif not (self.is_wheel or self.is_image or self.is_pad):
            entry["label"] = self.text
        entry.update({"x": self.x, "y": self.y, "width": self.width, "height": self.height})
        if self.is_button:
            if self.icon != "none":
                entry["icon"] = self.icon
            if self.shape != "rectangle":
                entry["shape"] = self.shape
            if self.navigate:
                entry["navigate"] = self.navigate
        if self.is_slider or self.is_dial:
            entry.update({"min": self.minimum, "max": self.maximum,
                          "step": self.step, "value": self.value})
        if self.is_wheel:
            entry.update({"items": list(self.items), "value": self.value})
        if self.is_number:
            entry.update({"unit": self.unit, "decimals": self.decimals, "value": self.value})
        if self.is_image:
            entry["source"] = self.source
            if self.transparent is not None:
                entry["transparent"] = self.transparent
        if self.is_pad:
            entry["center"] = self.center
        for event in CALLBACK_EVENTS:
            name = getattr(self, event)
            if name:
                entry[event] = name
        if self.feature:
            entry["feature"] = self.feature
        if self.operation:
            entry["operation"] = self.operation
        if not self.is_image:
            entry["style"] = self.style.to_document()
        return entry


# Backwards-compatible alias used by older callers/tests.
Button = Widget


@dataclass
class Screen:
    id: str
    background: str   # CAMERA_BACKGROUND or "#RRGGBB"
    widgets: list[Widget]

    @property
    def is_camera(self) -> bool:
        return self.background == CAMERA_BACKGROUND

    def buttons(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_button]

    def labels(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_label]

    def sliders(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_slider]

    def dials(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_dial]

    def wheels(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_wheel]

    def numbers(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_number]

    def images(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_image]

    def pads(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_pad]

    def to_document(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "background": self.background,
            "widgets": [widget.to_document() for widget in self.widgets],
        }


@dataclass
class Layout:
    width: int
    height: int
    namespace: str
    screens: list[Screen]

    @property
    def widgets(self) -> list[Widget]:
        """All widgets across screens, in WidgetId order."""
        return [w for screen in self.screens for w in screen.widgets]

    def buttons(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_button]

    def labels(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_label]

    def sliders(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_slider]

    def dials(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_dial]

    def wheels(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_wheel]

    def numbers(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_number]

    def images(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_image]

    def pads(self) -> list[Widget]:
        return [w for w in self.widgets if w.is_pad]

    def screen(self, screen_id: str) -> Screen:
        for screen in self.screens:
            if screen.id == screen_id:
                return screen
        raise LayoutError(f"no screen with id {screen_id!r}")

    def to_document(self) -> dict[str, Any]:
        return {
            "schema_version": SCHEMA_VERSION,
            "screen": {"width": self.width, "height": self.height},
            "namespace": self.namespace,
            "screens": [screen.to_document() for screen in self.screens],
        }


def _int(value: Any, context: str, minimum: int | None = 0,
         maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise LayoutError(f"{context} must be an integer, got {value!r}")
    if (minimum is not None and value < minimum) or (maximum is not None and value > maximum):
        lower = "" if minimum is None else f" >= {minimum}"
        upper = "" if maximum is None else f" <= {maximum}"
        raise LayoutError(f"{context} must be{lower}{upper}, got {value}")
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


def _slider_style(value: Any, context: str) -> SliderStyle:
    merged = _style_mapping(value, SLIDER_STYLE_DEFAULTS, context)
    if not isinstance(merged["show_value"], bool):
        raise LayoutError(f"{context}.show_value must be true or false")
    return SliderStyle(
        track=_color(merged["track"], f"{context}.track"),
        fill=_color(merged["fill"], f"{context}.fill"),
        knob=_color(merged["knob"], f"{context}.knob"),
        text=_color(merged["text"], f"{context}.text"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        show_value=merged["show_value"],
    )


def _dial_style(value: Any, context: str) -> DialStyle:
    merged = _style_mapping(value, DIAL_STYLE_DEFAULTS, context)
    if not isinstance(merged["show_value"], bool):
        raise LayoutError(f"{context}.show_value must be true or false")
    return DialStyle(
        face=_color(merged["face"], f"{context}.face"),
        track=_color(merged["track"], f"{context}.track"),
        fill=_color(merged["fill"], f"{context}.fill"),
        pointer=_color(merged["pointer"], f"{context}.pointer"),
        text=_color(merged["text"], f"{context}.text"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        show_value=merged["show_value"],
    )


def _wheel_style(value: Any, context: str) -> WheelStyle:
    merged = _style_mapping(value, WHEEL_STYLE_DEFAULTS, context)
    return WheelStyle(
        fill=_color(merged["fill"], f"{context}.fill"),
        highlight=_color(merged["highlight"], f"{context}.highlight"),
        text=_color(merged["text"], f"{context}.text"),
        selected_text=_color(merged["selected_text"], f"{context}.selected_text"),
        border=_color(merged["border"], f"{context}.border"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
    )


def _number_style(value: Any, context: str) -> NumberStyle:
    merged = _style_mapping(value, NUMBER_STYLE_DEFAULTS, context)
    align = merged["align"]
    if align not in ALIGNMENTS:
        raise LayoutError(f"{context}.align must be one of {ALIGNMENTS}, got {align!r}")
    return NumberStyle(
        text=_color(merged["text"], f"{context}.text"),
        fill=None if merged["fill"] is None else _color(merged["fill"], f"{context}.fill"),
        text_scale=_int(merged["text_scale"], f"{context}.text_scale", 1, 16),
        align=align,
    )


def _pad_style(value: Any, context: str) -> PadStyle:
    merged = _style_mapping(value, PAD_STYLE_DEFAULTS, context)
    return PadStyle(
        fill=_color(merged["fill"], f"{context}.fill"),
        pressed_fill=_color(merged["pressed_fill"], f"{context}.pressed_fill"),
        center_fill=_color(merged["center_fill"], f"{context}.center_fill"),
        border=_color(merged["border"], f"{context}.border"),
        arrow=_color(merged["arrow"], f"{context}.arrow"),
        border_width=_int(merged["border_width"], f"{context}.border_width", 0, 32),
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


def _reject_keys(raw: dict[str, Any], keys: tuple[str, ...], context: str, kind: str) -> None:
    for key in keys:
        if key in raw:
            raise LayoutError(f"{context}: {kind}s do not support {key!r}")


def _identifier(value: Any, context: str) -> str:
    if not isinstance(value, str) or not IDENTIFIER_RE.match(value):
        raise LayoutError(f"{context} must match [a-z][a-z0-9_]*, got {value!r}")
    return value


def parse_feature_catalog(document: Any) -> dict[str, frozenset[str]]:
    if not isinstance(document, dict) or set(document) != {"features"} or not isinstance(document["features"], list):
        raise LayoutError("feature catalog must contain a features list")
    catalog: dict[str, frozenset[str]] = {}
    for index, entry in enumerate(document["features"]):
        context = f"features[{index}]"
        if not isinstance(entry, dict) or set(entry) != {"id", "operations"} or not isinstance(entry["operations"], list):
            raise LayoutError(f"{context} needs an id and operations list")
        feature = _identifier(entry["id"], f"{context}.id")
        operations = [_identifier(operation, f"{context}.operations") for operation in entry["operations"]]
        if feature in catalog or len(operations) != len(set(operations)):
            raise LayoutError(f"{context} contains duplicate feature or operation ids")
        catalog[feature] = frozenset(operations)
    return catalog


def load_feature_catalog(path: Path) -> dict[str, frozenset[str]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise LayoutError(f"could not read feature catalog {path}: {error}") from error
    return parse_feature_catalog(document)


def _parse_widget(raw: Any, context: str, width: int, height: int,
                  allowed: frozenset[str]) -> Widget:
    if not isinstance(raw, dict):
        raise LayoutError(f"{context} must be an object")
    unknown = set(raw) - WIDGET_KEYS
    if unknown:
        raise LayoutError(f"{context} has unknown keys {sorted(unknown)}")
    widget_type = raw.get("type", "button")
    if widget_type not in WIDGET_TYPES:
        raise LayoutError(f"{context}.type {widget_type!r} is not one of {WIDGET_TYPES}")
    widget_id = _identifier(raw.get("id"), f"{context}.id")
    x = _int(raw.get("x"), f"{context}.x", 0, width - 1)
    y = _int(raw.get("y"), f"{context}.y", 0, height - 1)
    w = _int(raw.get("width"), f"{context}.width", 1, width - x)
    h = _int(raw.get("height"), f"{context}.height", 1, height - y)
    feature = raw.get("feature", "")
    operation = raw.get("operation", "")
    if feature != "":
        feature = _identifier(feature, f"{context}.feature")
    if operation != "":
        operation = _identifier(operation, f"{context}.operation")
    if operation and not feature:
        raise LayoutError(f"{context}.operation requires a feature")
    if operation and widget_type not in ("button", "slider", "dial", "wheel", "pad"):
        raise LayoutError(f"{context}: {widget_type}s do not support operations")
    base = dict(type=widget_type, id=widget_id, x=x, y=y, width=w, height=h,
                feature=feature, operation=operation)

    if widget_type == "button":
        _reject_keys(raw, ("text", "items", "unit", "decimals", "source", "transparent", "center")
                     + SLIDER_EVENTS + SLIDER_RANGE_KEYS, context, "button")
        icon = raw.get("icon", "none")
        if icon not in ICONS:
            raise LayoutError(f"{context}.icon must be one of {ICONS}, got {icon!r}")
        shape = raw.get("shape", "rectangle")
        if shape not in SHAPES:
            raise LayoutError(f"{context}.shape must be one of {SHAPES}, got {shape!r}")
        navigate = raw.get("navigate", "")
        if navigate:
            navigate = _identifier(navigate, f"{context}.navigate")
        return Widget(
            text=_text(raw, "label", context, allowed),
            style=_button_style(raw.get("style"), f"{context}.style"),
            on_tap=_callback(raw.get("on_tap"), f"{context}.on_tap"),
            on_press=_callback(raw.get("on_press"), f"{context}.on_press"),
            icon=icon, shape=shape, navigate=navigate, **base)

    if widget_type == "label":
        _reject_keys(raw, ("label", "icon", "shape", "navigate", "items", "unit", "decimals",
                           "source", "transparent", "center") + CALLBACK_EVENTS + SLIDER_RANGE_KEYS,
                     context, "label")
        text = _text(raw, "text", context, allowed)
        if len(text) >= LABEL_TEXT_CAPACITY:
            raise LayoutError(
                f"{context}.text must be shorter than {LABEL_TEXT_CAPACITY} characters")
        return Widget(text=text, style=_label_style(raw.get("style"), f"{context}.style"),
                      **base)

    if widget_type in VALUE_WIDGETS:
        _reject_keys(raw, ("text", "icon", "shape", "navigate", "items", "unit", "decimals",
                           "source", "transparent", "center") + BUTTON_EVENTS, context, widget_type)
        minimum = _int(raw.get("min", 0), f"{context}.min", None, None)
        maximum = _int(raw.get("max", 100), f"{context}.max", None, None)
        if maximum <= minimum:
            raise LayoutError(f"{context}: max must be greater than min")
        step = _int(raw.get("step", 1), f"{context}.step", 1, maximum - minimum)
        value = _int(raw.get("value", minimum), f"{context}.value", minimum, maximum)
        if widget_type == "slider":
            if h < 7 + 4 + SLIDER_TRACK_HEIGHT:
                raise LayoutError(f"{context}.height is too small for a caption and track")
            style: Style = _slider_style(raw.get("style"), f"{context}.style")
        else:
            if h < 7 + 4 + 16 or w < 16:
                raise LayoutError(f"{context} is too small for a caption and dial")
            style = _dial_style(raw.get("style"), f"{context}.style")
        return Widget(
            text=_text(raw, "label", context, allowed), style=style,
            on_change=_callback(raw.get("on_change"), f"{context}.on_change"),
            minimum=minimum, maximum=maximum, step=step, value=value, **base)

    if widget_type == "wheel":
        _reject_keys(raw, ("text", "label", "icon", "shape", "navigate", "unit", "decimals",
                           "source", "transparent", "center", "min", "max", "step") + BUTTON_EVENTS,
                     context, "wheel")
        items = raw.get("items")
        if (not isinstance(items, list) or not items or len(items) > MAX_WHEEL_ITEMS or
                not all(isinstance(item, str) for item in items)):
            raise LayoutError(f"{context}.items must be 1..{MAX_WHEEL_ITEMS} strings")
        for index, item in enumerate(items):
            _text({"item": item}, "item", f"{context}.items[{index}]", allowed)
        value = _int(raw.get("value", 0), f"{context}.value", 0, len(items) - 1)
        return Widget(
            text="", style=_wheel_style(raw.get("style"), f"{context}.style"),
            on_change=_callback(raw.get("on_change"), f"{context}.on_change"),
            items=list(items), value=value, **base)

    if widget_type == "number":
        _reject_keys(raw, ("text", "icon", "shape", "navigate", "items", "source",
                           "transparent", "center", "min", "max", "step") + CALLBACK_EVENTS,
                     context, "number")
        unit = _text(raw, "unit", context, allowed)
        if len(unit) > 8:
            raise LayoutError(f"{context}.unit must be at most 8 characters")
        return Widget(
            text=_text(raw, "label", context, allowed),
            style=_number_style(raw.get("style"), f"{context}.style"),
            unit=unit,
            decimals=_int(raw.get("decimals", 0), f"{context}.decimals", 0, 6),
            value=_int(raw.get("value", 0), f"{context}.value", -2**31, 2**31 - 1), **base)

    if widget_type == "pad":
        _reject_keys(raw, ("text", "label", "icon", "shape", "navigate", "items", "unit",
                           "decimals", "source", "transparent") + SLIDER_RANGE_KEYS,
                     context, "pad")
        if w < PAD_MIN_SIDE or h < PAD_MIN_SIDE:
            raise LayoutError(f"{context}: pads must be at least {PAD_MIN_SIDE}x{PAD_MIN_SIDE}")
        center = raw.get("center", True)
        if not isinstance(center, bool):
            raise LayoutError(f"{context}.center must be true or false")
        return Widget(
            text="", style=_pad_style(raw.get("style"), f"{context}.style"),
            on_tap=_callback(raw.get("on_tap"), f"{context}.on_tap"),
            on_press=_callback(raw.get("on_press"), f"{context}.on_press"),
            on_change=_callback(raw.get("on_change"), f"{context}.on_change"),
            center=center, **base)

    _reject_keys(raw, ("text", "label", "icon", "shape", "navigate", "items", "unit",
                       "decimals", "style", "center") + CALLBACK_EVENTS + SLIDER_RANGE_KEYS,
                 context, "image")
    source = raw.get("source")
    if not isinstance(source, str) or not source or source.startswith(("/", "..")):
        raise LayoutError(f"{context}.source must be a relative path below the layout file")
    if w * h * 2 > IMAGE_MAX_BYTES:
        raise LayoutError(
            f"{context}: {w}x{h} RGB565 bitmap exceeds {IMAGE_MAX_BYTES} bytes")
    transparent = raw.get("transparent")
    return Widget(
        text="", style=ImageStyle(), source=source,
        transparent=None if transparent is None else _color(transparent, f"{context}.transparent"),
        **base)


def _parse_screen(raw: Any, context: str, width: int, height: int,
                  allowed: frozenset[str]) -> Screen:
    if not isinstance(raw, dict):
        raise LayoutError(f"{context} must be an object")
    unknown = set(raw) - SCREEN_KEYS
    if unknown:
        raise LayoutError(f"{context} has unknown keys {sorted(unknown)}")
    screen_id = _identifier(raw.get("id"), f"{context}.id")
    background = raw.get("background", CAMERA_BACKGROUND)
    if background != CAMERA_BACKGROUND:
        background = _color(background, f"{context}.background")
    widgets_raw = raw.get("widgets", [])
    if not isinstance(widgets_raw, list):
        raise LayoutError(f"{context}.widgets must be a list")
    widgets = [_parse_widget(w, f"{context}.widgets[{i}]", width, height, allowed)
               for i, w in enumerate(widgets_raw)]
    if sum(widget.is_pad for widget in widgets) > MAX_PADS:
        raise LayoutError(f"{context}: at most {MAX_PADS} pads are supported per screen")
    for i, a in enumerate(widgets):
        for b in widgets[i + 1:]:
            if (a.x < b.x + b.width and b.x < a.x + a.width and
                    a.y < b.y + b.height and b.y < a.y + a.height):
                raise LayoutError(f"widgets {a.id!r} and {b.id!r} overlap on screen {screen_id!r}")
    return Screen(id=screen_id, background=background, widgets=widgets)


def parse_layout(document: Any, features: dict[str, frozenset[str]] | None = None) -> Layout:
    if not isinstance(document, dict):
        raise LayoutError("layout root must be an object")
    version = document.get("schema_version", SCHEMA_VERSION)
    if version not in (1, SCHEMA_VERSION):
        raise LayoutError(f"unsupported schema_version {version!r}")
    screen = document.get("screen")
    if not isinstance(screen, dict):
        raise LayoutError("screen must be an object with width and height")
    width = _int(screen.get("width"), "screen.width", 1, 4096)
    height = _int(screen.get("height"), "screen.height", 1, 4096)
    namespace = document.get("namespace", "uai::ai::app_ui")
    if not isinstance(namespace, str) or not NAMESPACE_RE.match(namespace):
        raise LayoutError(f"namespace must be a C++ qualified name, got {namespace!r}")

    if "screens" in document and "widgets" in document:
        raise LayoutError("use either 'screens' or the legacy top-level 'widgets'")
    screens_raw = document.get("screens")
    if screens_raw is None:
        screens_raw = [{"id": "main", "background": CAMERA_BACKGROUND,
                        "widgets": document.get("widgets", [])}]
    if not isinstance(screens_raw, list) or not screens_raw:
        raise LayoutError("screens must be a non-empty list")

    allowed = supported_characters()
    screens = [_parse_screen(s, f"screens[{i}]", width, height, allowed)
               for i, s in enumerate(screens_raw)]

    screen_ids = [s.id for s in screens]
    if len(set(screen_ids)) != len(screen_ids):
        raise LayoutError("screen ids must be unique")
    seen: set[str] = set()
    for widget in (w for s in screens for w in s.widgets):
        if widget.id in seen:
            raise LayoutError(f"widget id {widget.id!r} is duplicated")
        seen.add(widget.id)
        if widget.navigate and widget.navigate not in screen_ids:
            raise LayoutError(
                f"widget {widget.id!r} navigates to unknown screen {widget.navigate!r}")
        if widget.feature:
            if features is None:
                raise LayoutError(f"widget {widget.id!r} requires a feature catalog")
            if widget.feature not in features:
                raise LayoutError(f"widget {widget.id!r} references unknown feature {widget.feature!r}")
            if widget.operation and widget.operation not in features[widget.feature]:
                raise LayoutError(f"widget {widget.id!r} references unknown operation {widget.operation!r}")
    return Layout(width=width, height=height, namespace=namespace, screens=screens)


def load_layout(path: Path, features: dict[str, frozenset[str]] | None = None) -> Layout:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise LayoutError(f"could not read {path}: {error}") from error
    return parse_layout(document, features)


def dump_layout(layout: Layout) -> str:
    return json.dumps(layout.to_document(), indent=2) + "\n"


def save_layout(path: Path, layout: Layout) -> None:
    path.write_text(dump_layout(layout), encoding="utf-8")


def pascal_case(identifier: str) -> str:
    return "".join(part.capitalize() for part in identifier.split("_") if part)
