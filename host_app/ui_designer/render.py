"""Software renderer that mirrors ui::Canvas / ui::ButtonPanel::Paint.

Pixels are RGB565 values so the preview shows the exact device colors. PNG
output uses only the standard library.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

from .font import GLYPH_HEIGHT, GLYPH_WIDTH, GLYPH_ADVANCE, glyph_rows, text_width
from .schema import (
    SLIDER_KNOB_MARGIN,
    SLIDER_KNOB_WIDTH,
    SLIDER_TRACK_HEIGHT,
    Layout,
    Screen,
    Widget,
    rgb565,
)


class Canvas:
    def __init__(self, width: int, height: int, background: int = 0x0000):
        self.width = width
        self.height = height
        self.pixels = [background] * (width * height)

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        if x >= self.width or y >= self.height:
            return
        x_end = min(x + w, self.width)
        y_end = min(y + h, self.height)
        for row in range(y, y_end):
            base = row * self.width
            for column in range(x, x_end):
                self.pixels[base + column] = color

    def draw_frame(self, x: int, y: int, w: int, h: int, thickness: int, color: int) -> None:
        if thickness == 0 or w == 0 or h == 0:
            return
        t_x = min(thickness, w)
        t_y = min(thickness, h)
        self.fill_rect(x, y, w, t_y, color)
        self.fill_rect(x, y + h - t_y, w, t_y, color)
        self.fill_rect(x, y, t_x, h, color)
        self.fill_rect(x + w - t_x, y, t_x, h, color)

    def draw_text(self, x: int, y: int, text: str, scale: int, color: int) -> None:
        if scale == 0:
            return
        pen_x = x
        for letter in text:
            rows = glyph_rows(letter)
            for row in range(GLYPH_HEIGHT):
                bits = rows[row]
                for column in range(GLYPH_WIDTH):
                    if not bits & (1 << (GLYPH_WIDTH - 1 - column)):
                        continue
                    px = pen_x + column * scale
                    py = y + row * scale
                    if px >= self.width or py >= self.height:
                        continue
                    self.fill_rect(px, py, scale, scale, color)
            pen_x += GLYPH_ADVANCE * scale

    def draw_text_centered(self, x: int, y: int, w: int, h: int, text: str,
                           scale: int, color: int) -> None:
        tw = text_width(text, scale)
        th = GLYPH_HEIGHT * scale
        tx = x + (w - tw) // 2 if tw < w else x
        ty = y + (h - th) // 2 if th < h else y
        self.draw_text(tx, ty, text, scale, color)


def paint_icon(canvas: Canvas, x: int, y: int, w: int, h: int, icon: str, color: int) -> None:
    """Mirror DrawIcon in kernel/middleware/ui/widget.cpp."""
    extent = min(w, h) // 2
    if extent < 8:
        return
    x0 = x + (w - extent) // 2
    y0 = y + (h - extent) // 2
    bar = max(extent // 6, 1)
    if icon == "menu":
        for row in range(3):
            canvas.fill_rect(x0, y0 + row * (extent - bar) // 2, extent, bar, color)
    elif icon == "back":
        mid = y0 + extent // 2
        canvas.fill_rect(x0, mid - bar // 2, extent, bar, color)
        for i in range(extent // 2):
            canvas.fill_rect(x0 + i, mid - i, bar, bar, color)
            canvas.fill_rect(x0 + i, mid + i, bar, bar, color)
    elif icon == "close":
        i = 0
        while i + bar <= extent:
            canvas.fill_rect(x0 + i, y0 + i, bar, bar, color)
            canvas.fill_rect(x0 + extent - bar - i, y0 + i, bar, bar, color)
            i += 1


def paint_button(canvas: Canvas, button: Widget, pressed: bool = False,
                 checked: bool = False) -> None:
    style = button.style
    fill = style.pressed_fill if pressed else style.checked_fill if checked else style.fill
    canvas.fill_rect(button.x, button.y, button.width, button.height, rgb565(fill))
    canvas.draw_frame(button.x, button.y, button.width, button.height,
                      style.border_width, rgb565(style.border))
    if button.icon != "none":
        paint_icon(canvas, button.x, button.y, button.width, button.height,
                   button.icon, rgb565(style.text))
    else:
        canvas.draw_text_centered(button.x, button.y, button.width, button.height,
                                  button.text, style.text_scale, rgb565(style.text))


def slider_track(slider: Widget) -> tuple[int, int, int, int]:
    """Mirror ui::SliderPanel::TrackOf."""
    caption = GLYPH_HEIGHT * slider.style.text_scale + 4
    inset = SLIDER_KNOB_WIDTH // 2
    x = slider.x + inset
    width = slider.width - 2 * inset if slider.width > 2 * inset else 1
    lower_top = slider.y + caption
    lower_height = slider.height - caption if slider.height > caption else SLIDER_TRACK_HEIGHT
    y = lower_top + ((lower_height - SLIDER_TRACK_HEIGHT) // 2
                     if lower_height > SLIDER_TRACK_HEIGHT else 0)
    return x, y, width, SLIDER_TRACK_HEIGHT


def paint_slider(canvas: Canvas, slider: Widget, value: int | None = None) -> None:
    """Mirror ui::SliderPanel::Paint; `value` overrides the initial value."""
    style = slider.style
    current = slider.value if value is None else max(slider.minimum, min(slider.maximum, value))
    tx, ty, tw, th = slider_track(slider)
    span = slider.maximum - slider.minimum
    position = (current - slider.minimum) * (tw - 1) // span if span > 0 else 0
    canvas.draw_text(slider.x, slider.y, slider.text, style.text_scale, rgb565(style.text))
    if style.show_value:
        value_text = str(current)
        canvas.draw_text(slider.x + slider.width - text_width(value_text, style.text_scale),
                         slider.y, value_text, style.text_scale, rgb565(style.text))
    canvas.fill_rect(tx, ty, tw, th, rgb565(style.track))
    canvas.fill_rect(tx, ty, position + 1, th, rgb565(style.fill))
    canvas.fill_rect(tx + position - SLIDER_KNOB_WIDTH // 2, ty - SLIDER_KNOB_MARGIN,
                     SLIDER_KNOB_WIDTH, th + 2 * SLIDER_KNOB_MARGIN, rgb565(style.knob))


def paint_label(canvas: Canvas, label: Widget, text: str | None = None) -> None:
    """Mirror ui::LabelPanel::Paint; `text` overrides the initial text."""
    style = label.style
    if style.fill is not None:
        canvas.fill_rect(label.x, label.y, label.width, label.height, rgb565(style.fill))
    content = label.text if text is None else text
    tw = text_width(content, style.text_scale)
    th = GLYPH_HEIGHT * style.text_scale
    padding = style.padding if 2 * style.padding < label.width else 0
    inner = label.width - 2 * padding
    x = label.x + padding
    if tw < inner:
        if style.align == "center":
            x += (inner - tw) // 2
        elif style.align == "right":
            x += inner - tw
    y = label.y + (label.height - th) // 2 if th < label.height else label.y
    canvas.draw_text(x, y, content, style.text_scale, rgb565(style.text))


def render_screen(layout: Layout, screen: Screen,
                  pressed_ids: frozenset[str] = frozenset(),
                  checked_ids: frozenset[str] = frozenset(),
                  camera_stand_in: int = 0x4208) -> Canvas:
    """Draw one screen exactly as ui::Screen::Paint would.

    A camera screen uses a flat stand-in color for the live frame; a solid
    screen uses its own background. Order: buttons, sliders, labels.
    """
    background = camera_stand_in if screen.is_camera else rgb565(screen.background)
    canvas = Canvas(layout.width, layout.height, background)
    for button in screen.buttons():
        paint_button(canvas, button, button.id in pressed_ids, button.id in checked_ids)
    for slider in screen.sliders():
        paint_slider(canvas, slider)
    for label in screen.labels():
        paint_label(canvas, label)
    return canvas


def render_layout(layout: Layout, pressed_ids: frozenset[str] = frozenset(),
                  checked_ids: frozenset[str] = frozenset(),
                  background: int = 0x4208, screen_id: str | None = None) -> Canvas:
    """Render the named screen (default: the first one)."""
    screen = layout.screens[0] if screen_id is None else layout.screen(screen_id)
    return render_screen(layout, screen, pressed_ids, checked_ids, background)


def rgb565_to_rgb888(value: int) -> tuple[int, int, int]:
    red = (value >> 11) & 0x1F
    green = (value >> 5) & 0x3F
    blue = value & 0x1F
    return (red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2)


def encode_png(canvas: Canvas) -> bytes:
    raw = bytearray()
    for row in range(canvas.height):
        raw.append(0)
        base = row * canvas.width
        for column in range(canvas.width):
            raw.extend(rgb565_to_rgb888(canvas.pixels[base + column]))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", canvas.width, canvas.height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def write_png(path: Path, canvas: Canvas) -> None:
    path.write_bytes(encode_png(canvas))
