"""Layout document model and validation for on-screen UI definitions.

The document is JSON (``ui_layout.json``). It is the single source of truth:
the browser editor edits it, ``render`` previews it, and ``generate`` turns it
into a C++ header consumed by ``kernel/middleware/ui``.

Document shape (schema_version 2)::

    {"screen": {...}, "namespace": "...",
     "screens": [{"id": "main", "background": "camera" | "#RRGGBB",
                  "widgets": [...]}, ...]}

Widget kinds:
  button  tappable; ``label`` or ``icon``, ButtonStyle, on_tap/on_press,
          optional ``navigate`` to another screen id
  label   text the firmware replaces at run time; ``text`` is the initial value
  slider  horizontal value control; min/max/step/value, on_change

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

WIDGET_TYPES = ("button", "label", "slider")
# Event names map to ui::EventType members and to the per-widget callback keys.
CALLBACK_EVENTS: tuple[str, ...] = ("on_tap", "on_press", "on_change")
BUTTON_EVENTS: tuple[str, ...] = ("on_tap", "on_press")
SLIDER_EVENTS: tuple[str, ...] = ("on_change",)
GEOMETRY_KEYS = ("x", "y", "width", "height")
SLIDER_RANGE_KEYS = ("min", "max", "step", "value")
WIDGET_KEYS = frozenset(
    ("type", "id", "label", "text", "style", "icon", "navigate")
    + GEOMETRY_KEYS + CALLBACK_EVENTS + SLIDER_RANGE_KEYS)
SCREEN_KEYS = frozenset(("id", "background", "widgets"))
ALIGNMENTS = ("left", "center", "right")
ICONS = ("none", "menu", "back", "close")
CAMERA_BACKGROUND = "camera"

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
STYLE_DEFAULTS: dict[str, dict[str, Any]] = {
    "button": BUTTON_STYLE_DEFAULTS,
    "label": LABEL_STYLE_DEFAULTS,
    "slider": SLIDER_STYLE_DEFAULTS,
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
class Widget:
    type: str
    id: str
    text: str
    x: int
    y: int
    width: int
    height: int
    style: ButtonStyle | LabelStyle | SliderStyle = field(default_factory=ButtonStyle)
    # C++ method names invoked by the generated Dispatch(); empty means none.
    on_tap: str = ""
    on_press: str = ""
    on_change: str = ""
    icon: str = "none"
    navigate: str = ""
    minimum: int = 0
    maximum: int = 100
    step: int = 1
    value: int = 0

    @property
    def is_button(self) -> bool:
        return self.type == "button"

    @property
    def is_label(self) -> bool:
        return self.type == "label"

    @property
    def is_slider(self) -> bool:
        return self.type == "slider"

    def to_document(self) -> dict[str, Any]:
        entry: dict[str, Any] = {
            "type": self.type,
            "id": self.id,
            "text" if self.is_label else "label": self.text,
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
        }
        if self.is_button:
            if self.icon != "none":
                entry["icon"] = self.icon
            if self.navigate:
                entry["navigate"] = self.navigate
        if self.is_slider:
            entry.update({"min": self.minimum, "max": self.maximum,
                          "step": self.step, "value": self.value})
        for event in CALLBACK_EVENTS:
            name = getattr(self, event)
            if name:
                entry[event] = name
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
    base = dict(type=widget_type, id=widget_id, x=x, y=y, width=w, height=h)

    if widget_type == "button":
        _reject_keys(raw, ("text",) + SLIDER_EVENTS + SLIDER_RANGE_KEYS, context, "button")
        icon = raw.get("icon", "none")
        if icon not in ICONS:
            raise LayoutError(f"{context}.icon must be one of {ICONS}, got {icon!r}")
        navigate = raw.get("navigate", "")
        if navigate:
            navigate = _identifier(navigate, f"{context}.navigate")
        return Widget(
            text=_text(raw, "label", context, allowed),
            style=_button_style(raw.get("style"), f"{context}.style"),
            on_tap=_callback(raw.get("on_tap"), f"{context}.on_tap"),
            on_press=_callback(raw.get("on_press"), f"{context}.on_press"),
            icon=icon, navigate=navigate, **base)

    if widget_type == "label":
        _reject_keys(raw, ("label", "icon", "navigate") + CALLBACK_EVENTS + SLIDER_RANGE_KEYS,
                     context, "label")
        text = _text(raw, "text", context, allowed)
        if len(text) >= LABEL_TEXT_CAPACITY:
            raise LayoutError(
                f"{context}.text must be shorter than {LABEL_TEXT_CAPACITY} characters")
        return Widget(text=text, style=_label_style(raw.get("style"), f"{context}.style"),
                      **base)

    _reject_keys(raw, ("text", "icon", "navigate") + BUTTON_EVENTS, context, "slider")
    minimum = _int(raw.get("min", 0), f"{context}.min", None, None)
    maximum = _int(raw.get("max", 100), f"{context}.max", None, None)
    if maximum <= minimum:
        raise LayoutError(f"{context}: max must be greater than min")
    step = _int(raw.get("step", 1), f"{context}.step", 1, maximum - minimum)
    value = _int(raw.get("value", minimum), f"{context}.value", minimum, maximum)
    if h < 7 * 1 + 4 + SLIDER_TRACK_HEIGHT:
        raise LayoutError(f"{context}.height is too small for a caption and track")
    return Widget(
        text=_text(raw, "label", context, allowed),
        style=_slider_style(raw.get("style"), f"{context}.style"),
        on_change=_callback(raw.get("on_change"), f"{context}.on_change"),
        minimum=minimum, maximum=maximum, step=step, value=value, **base)


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
    for i, a in enumerate(widgets):
        for b in widgets[i + 1:]:
            if (a.x < b.x + b.width and b.x < a.x + a.width and
                    a.y < b.y + b.height and b.y < a.y + a.height):
                raise LayoutError(f"widgets {a.id!r} and {b.id!r} overlap on screen {screen_id!r}")
    return Screen(id=screen_id, background=background, widgets=widgets)


def parse_layout(document: Any) -> Layout:
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
    return Layout(width=width, height=height, namespace=namespace, screens=screens)


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
