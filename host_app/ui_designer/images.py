"""Convert layout image widgets into RGB565 bitmaps for the firmware.

The PNG referenced by ``source`` (relative to the layout file) is resampled
with nearest-neighbour to the widget bounds. Transparent PNG pixels (alpha
below 128) become the widget's ``transparent`` key color so the device can
skip them; opaque pixels that happen to equal the key are nudged by one
blue level.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .png import PngError, read_png
from .schema import DEFAULT_TRANSPARENT_KEY, LayoutError, Widget, rgb565


@dataclass
class Bitmap:
    width: int
    height: int
    pixels: list[int]          # RGB565, row-major
    transparent: int | None    # RGB565 key or None


def resolve_source(layout_path: Path, widget: Widget) -> Path:
    return (layout_path.parent / widget.source).resolve()


def load_bitmap(layout_path: Path, widget: Widget) -> Bitmap:
    path = resolve_source(layout_path, widget)
    try:
        width, height, rgba = read_png(path)
    except (OSError, PngError) as error:
        raise LayoutError(f"image {widget.id!r}: {error}") from error
    key_color = widget.transparent
    has_alpha = any(a < 128 for *_, a in rgba)
    if has_alpha and key_color is None:
        key_color = DEFAULT_TRANSPARENT_KEY
    key = rgb565(key_color) if key_color is not None else None

    pixels: list[int] = []
    for row in range(widget.height):
        source_y = row * height // widget.height
        for column in range(widget.width):
            source_x = column * width // widget.width
            r, g, b, a = rgba[source_y * width + source_x]
            if key is not None and a < 128:
                pixels.append(key)
                continue
            value = rgb565(f"#{r:02X}{g:02X}{b:02X}")
            if key is not None and value == key:
                value ^= 0x0001
            pixels.append(value)
    return Bitmap(widget.width, widget.height, pixels, key)
