"""Software renderer that mirrors ui::Canvas / ui::ButtonPanel::Paint.

Pixels are RGB565 values so the preview shows the exact device colors. PNG
output uses only the standard library.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

from .font import GLYPH_HEIGHT, GLYPH_WIDTH, GLYPH_ADVANCE, glyph_rows, text_width
from .schema import Button, Layout, rgb565


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


def paint_button(canvas: Canvas, button: Button, pressed: bool = False) -> None:
    style = button.style
    canvas.fill_rect(button.x, button.y, button.width, button.height,
                     rgb565(style.pressed_fill if pressed else style.fill))
    canvas.draw_frame(button.x, button.y, button.width, button.height,
                      style.border_width, rgb565(style.border))
    canvas.draw_text_centered(button.x, button.y, button.width, button.height,
                              button.label, style.text_scale, rgb565(style.text))


def render_layout(layout: Layout, pressed_ids: frozenset[str] = frozenset(),
                  background: int = 0x4208) -> Canvas:
    """Draw every widget on a flat background standing in for the camera."""
    canvas = Canvas(layout.width, layout.height, background)
    for button in layout.widgets:
        paint_button(canvas, button, button.id in pressed_ids)
    return canvas


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
