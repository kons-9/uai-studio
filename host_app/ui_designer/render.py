"""Software renderer that mirrors ui::Canvas / ui::ButtonPanel::Paint.

Pixels are RGB565 values so the preview shows the exact device colors. PNG
output uses only the standard library.
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

from .font import GLYPH_HEIGHT, GLYPH_WIDTH, GLYPH_ADVANCE, glyph_rows, text_width
from .images import Bitmap
from .schema import (
    SLIDER_KNOB_MARGIN,
    SLIDER_KNOB_WIDTH,
    SLIDER_TRACK_HEIGHT,
    Layout,
    Screen,
    Widget,
    rgb565,
)


def inside_ellipse(x: int, y: int, w: int, h: int, px: int, py: int) -> bool:
    """Mirror ui::InsideEllipse (doubled coordinates, integer math)."""
    if w == 0 or h == 0:
        return False
    dx = 2 * (px - x) + 1 - w
    dy = 2 * (py - y) + 1 - h
    return dx * dx * h * h + dy * dy * w * w <= w * w * h * h


def corner_radius(shape: str, w: int, h: int) -> int:
    side = min(w, h)
    if shape == "rounded":
        return side // 4
    if shape == "pill":
        return side // 2
    return 0


def inside_shape(shape: str, x: int, y: int, w: int, h: int, px: int, py: int) -> bool:
    """Mirror ui::InsideShape in canvas.hpp."""
    if px < x or py < y or px >= x + w or py >= y + h:
        return False
    if shape == "rectangle":
        return True
    if shape == "ellipse":
        return inside_ellipse(x, y, w, h, px, py)
    dx = 2 * (px - x) + 1 - w
    dy = 2 * (py - y) + 1 - h
    ax, ay = abs(dx), abs(dy)
    if shape in ("rounded", "pill"):
        r2 = 2 * corner_radius(shape, w, h)
        cx, cy = w - r2, h - r2
        if ax <= cx or ay <= cy:
            return True
        return (ax - cx) ** 2 + (ay - cy) ** 2 <= r2 * r2
    if shape == "diamond":
        return ax * h + ay * w <= w * h
    if shape == "triangle_up":
        return ax * 2 * h <= w * (dy + h + 1)
    if shape == "triangle_down":
        return ax * 2 * h <= w * (h - dy + 1)
    if shape == "triangle_left":
        return ay * 2 * w <= h * (dx + w + 1)
    if shape == "triangle_right":
        return ay * 2 * w <= h * (w - dx + 1)
    return True


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

    def put_pixel(self, x: int, y: int, color: int) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y * self.width + x] = color

    def fill_ellipse(self, x: int, y: int, w: int, h: int, color: int) -> None:
        self.fill_shape("ellipse", x, y, w, h, color)

    def draw_ellipse_frame(self, x: int, y: int, w: int, h: int, thickness: int, color: int) -> None:
        self.draw_shape_frame("ellipse", x, y, w, h, thickness, color)

    def fill_shape(self, shape: str, x: int, y: int, w: int, h: int, color: int) -> None:
        if shape == "rectangle":
            self.fill_rect(x, y, w, h, color)
            return
        for py in range(y, min(y + h, self.height)):
            for px in range(x, min(x + w, self.width)):
                if inside_shape(shape, x, y, w, h, px, py):
                    self.pixels[py * self.width + px] = color

    def draw_shape_frame(self, shape: str, x: int, y: int, w: int, h: int,
                         thickness: int, color: int) -> None:
        if thickness == 0:
            return
        if shape == "rectangle":
            self.draw_frame(x, y, w, h, thickness, color)
            return
        if 2 * thickness >= w or 2 * thickness >= h:
            self.fill_shape(shape, x, y, w, h, color)
            return
        ix, iy, iw, ih = x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness
        for py in range(y, min(y + h, self.height)):
            for px in range(x, min(x + w, self.width)):
                if (inside_shape(shape, x, y, w, h, px, py) and
                        not inside_shape(shape, ix, iy, iw, ih, px, py)):
                    self.pixels[py * self.width + px] = color

    def blit(self, x: int, y: int, bitmap: Bitmap) -> None:
        for row in range(bitmap.height):
            py = y + row
            if py >= self.height:
                break
            for column in range(bitmap.width):
                px = x + column
                if px >= self.width:
                    break
                value = bitmap.pixels[row * bitmap.width + column]
                if bitmap.transparent is not None and value == bitmap.transparent:
                    continue
                self.pixels[py * self.width + px] = value

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
    canvas.fill_shape(button.shape, button.x, button.y, button.width, button.height, rgb565(fill))
    canvas.draw_shape_frame(button.shape, button.x, button.y, button.width, button.height,
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
                  camera_stand_in: int = 0x4208,
                  bitmaps: dict[str, Bitmap] | None = None) -> Canvas:
    """Draw one screen exactly as ui::Screen::Paint would.

    A camera screen uses a flat stand-in color for the live frame; a solid
    screen uses its own background. Order: images, buttons, sliders, dials,
    wheels, pads, numbers, labels. Images without a bitmap are drawn as a
    frame.
    """
    background = camera_stand_in if screen.is_camera else rgb565(screen.background)
    canvas = Canvas(layout.width, layout.height, background)
    for image in screen.images():
        bitmap = (bitmaps or {}).get(image.id)
        if bitmap is not None:
            canvas.blit(image.x, image.y, bitmap)
        else:
            canvas.draw_frame(image.x, image.y, image.width, image.height, 1, 0xF81F)
    for button in screen.buttons():
        paint_button(canvas, button, button.id in pressed_ids, button.id in checked_ids)
    for slider in screen.sliders():
        paint_slider(canvas, slider)
    for dial in screen.dials():
        paint_dial(canvas, dial)
    for wheel in screen.wheels():
        paint_wheel(canvas, wheel)
    for pad in screen.pads():
        paint_pad(canvas, pad)
    for number in screen.numbers():
        paint_number(canvas, number)
    for label in screen.labels():
        paint_label(canvas, label)
    return canvas


def render_layout(layout: Layout, pressed_ids: frozenset[str] = frozenset(),
                  checked_ids: frozenset[str] = frozenset(),
                  background: int = 0x4208, screen_id: str | None = None,
                  bitmaps: dict[str, Bitmap] | None = None) -> Canvas:
    """Render the named screen (default: the first one)."""
    screen = layout.screens[0] if screen_id is None else layout.screen(screen_id)
    return render_screen(layout, screen, pressed_ids, checked_ids, background, bitmaps)


# --- dial -------------------------------------------------------------------

DIAL_SWEEP = 270
DIAL_START = 135


def _degrees(dx2: int, dy2: int) -> int:
    """Mirror DegreesOf in widget.cpp: clockwise degrees, rounded."""
    radians = math.atan2(dy2, dx2)
    degrees = int(radians * 180.0 / math.pi + (0.5 if radians >= 0 else -0.5))
    if degrees < 0:
        degrees += 360
    return degrees % 360


def _sweep(dx2: int, dy2: int) -> int:
    sweep = (_degrees(dx2, dy2) - DIAL_START + 360) % 360
    return sweep if sweep <= DIAL_SWEEP else -1


# --- pad --------------------------------------------------------------------

PAD_CENTER = 4


def pad_center(pad: Widget) -> tuple[int, int, int]:
    """Mirror ui::PadPanel::CenterOf: (x, y, side)."""
    side = min(pad.width, pad.height) * 2 // 5
    return pad.x + (pad.width - side) // 2, pad.y + (pad.height - side) // 2, side


def pad_angle(pad: Widget, px: int, py: int) -> int:
    """Mirror ui::PadPanel::AngleAt: clockwise degrees from the top."""
    dx2 = 2 * (px - pad.x) + 1 - pad.width
    dy2 = 2 * (py - pad.y) + 1 - pad.height
    return (_degrees(dx2, dy2) + 90) % 360


def pad_segment(pad: Widget, px: int, py: int) -> int:
    """Mirror ui::PadPanel::SegmentAt: 0 up, 1 right, 2 down, 3 left, 4 centre, -1 outside."""
    if not inside_ellipse(pad.x, pad.y, pad.width, pad.height, px, py):
        return -1
    cx, cy, side = pad_center(pad)
    if pad.center and inside_ellipse(cx, cy, side, side, px, py):
        return PAD_CENTER
    offset_x = 2 * (px - pad.x) + 1 - pad.width
    offset_y = 2 * (py - pad.y) + 1 - pad.height
    if offset_x >= 0:
        if offset_y < -offset_x:
            return 0
        if offset_y >= offset_x:
            return 2
        return 1
    if offset_y > -offset_x:
        return 2
    if offset_y <= offset_x:
        return 0
    return 3


def paint_pad(canvas: Canvas, pad: Widget, pressed: int = -1) -> None:
    """Mirror ui::PadPanel::Paint; `pressed` is the held segment or -1."""
    style = pad.style
    cx, cy, side = pad_center(pad)
    for py in range(pad.y, min(pad.y + pad.height, canvas.height)):
        for px in range(pad.x, min(pad.x + pad.width, canvas.width)):
            segment = pad_segment(pad, px, py)
            if segment < 0:
                continue
            color = (style.pressed_fill if segment == pressed
                     else style.center_fill if segment == PAD_CENTER else style.fill)
            canvas.pixels[py * canvas.width + px] = rgb565(color)
    canvas.draw_ellipse_frame(pad.x, pad.y, pad.width, pad.height, style.border_width,
                              rgb565(style.border))
    if pad.center:
        canvas.draw_ellipse_frame(cx, cy, side, side, style.border_width, rgb565(style.border))
    short = min(pad.width, pad.height)
    arrow = max(short // 8, 4)
    mx, my = pad.x + pad.width // 2, pad.y + pad.height // 2
    rx = (pad.width // 2 + side // 2) // 2
    ry = (pad.height // 2 + side // 2) // 2
    for ax, ay, shape in ((mx, my - ry, "triangle_up"), (mx + rx, my, "triangle_right"),
                          (mx, my + ry, "triangle_down"), (mx - rx, my, "triangle_left")):
        canvas.fill_shape(shape, ax - arrow // 2, ay - arrow // 2, arrow, arrow, rgb565(style.arrow))


def dial_disc(dial: Widget) -> tuple[int, int, int]:
    """Mirror ui::DialPanel::DiscOf: (x, y, side)."""
    caption = GLYPH_HEIGHT * dial.style.text_scale + 4
    free_height = dial.height - caption if dial.height > caption else 1
    side = min(dial.width, free_height)
    return (dial.x + (dial.width - side) // 2,
            dial.y + caption + (free_height - side) // 2, side)


def dial_value_at(dial: Widget, px: int, py: int) -> int:
    """Mirror ui::DialPanel::ValueAt."""
    x, y, side = dial_disc(dial)
    dx2 = 2 * (px - x) + 1 - side
    dy2 = 2 * (py - y) + 1 - side
    sweep = _sweep(dx2, dy2)
    if sweep < 0:
        sweep = 0 if dx2 < 0 else DIAL_SWEEP
    span = dial.maximum - dial.minimum
    raw = dial.minimum + (sweep * span + DIAL_SWEEP // 2) // DIAL_SWEEP
    snapped = dial.minimum + ((raw - dial.minimum + dial.step // 2) // dial.step) * dial.step
    return max(dial.minimum, min(dial.maximum, snapped))


def paint_dial(canvas: Canvas, dial: Widget, value: int | None = None) -> None:
    """Mirror ui::DialPanel::Paint."""
    style = dial.style
    current_value = dial.value if value is None else max(dial.minimum, min(dial.maximum, value))
    x, y, side = dial_disc(dial)
    outer = side
    thickness = max(side // 8, 4)
    inner = outer - 2 * thickness
    span = dial.maximum - dial.minimum
    current = (current_value - dial.minimum) * DIAL_SWEEP // span if span > 0 else 0
    canvas.draw_text(dial.x, dial.y, dial.text, style.text_scale, rgb565(style.text))
    if style.show_value:
        value_text = str(current_value)
        canvas.draw_text(dial.x + dial.width - text_width(value_text, style.text_scale), dial.y,
                         value_text, style.text_scale, rgb565(style.text))
    face, track, fill = rgb565(style.face), rgb565(style.track), rgb565(style.fill)
    for py in range(y, y + side):
        for px in range(x, x + side):
            dx2 = 2 * (px - x) + 1 - side
            dy2 = 2 * (py - y) + 1 - side
            d2 = dx2 * dx2 + dy2 * dy2
            if d2 > outer * outer:
                continue
            if d2 <= inner * inner:
                canvas.put_pixel(px, py, face)
                continue
            sweep = _sweep(dx2, dy2)
            if sweep < 0:
                continue
            canvas.put_pixel(px, py, fill if sweep <= current else track)
    angle = math.radians(DIAL_START + current)
    radius = (outer - thickness) / 2.0
    cx = x + side / 2.0
    cy = y + side / 2.0
    pointer = thickness // 2 + 2
    ppx = int(cx + radius * math.cos(angle))
    ppy = int(cy + radius * math.sin(angle))
    canvas.fill_ellipse(ppx - pointer, ppy - pointer, 2 * pointer, 2 * pointer, rgb565(style.pointer))


# --- wheel ------------------------------------------------------------------

def wheel_row_height(wheel: Widget) -> int:
    return GLYPH_HEIGHT * wheel.style.text_scale + 8


def paint_wheel(canvas: Canvas, wheel: Widget, selected: int | None = None) -> None:
    """Mirror ui::WheelPanel::Paint."""
    style = wheel.style
    current = wheel.value if selected is None else max(0, min(len(wheel.items) - 1, selected))
    row = wheel_row_height(wheel)
    band_y = wheel.y + (wheel.height - row) // 2
    canvas.fill_rect(wheel.x, wheel.y, wheel.width, wheel.height, rgb565(style.fill))
    canvas.fill_rect(wheel.x, band_y, wheel.width, row, rgb565(style.highlight))
    canvas.draw_frame(wheel.x, wheel.y, wheel.width, wheel.height, 1, rgb565(style.border))
    reach = (wheel.height // row) // 2 + 1
    for offset in range(-reach, reach + 1):
        item = current + offset
        if item < 0 or item >= len(wheel.items):
            continue
        top = band_y + offset * row
        if top < wheel.y or top + row > wheel.y + wheel.height:
            continue
        canvas.draw_text_centered(wheel.x, top, wheel.width, row, wheel.items[item],
                                  style.text_scale,
                                  rgb565(style.selected_text if offset == 0 else style.text))


# --- number -----------------------------------------------------------------

def format_number(number: Widget, value: int) -> str:
    """Mirror ui::NumberPanel::Format."""
    divisor = 10 ** number.decimals
    magnitude = abs(value)
    sign = "-" if value < 0 else ""
    if number.decimals == 0:
        return f"{sign}{magnitude}{number.unit}"
    return f"{sign}{magnitude // divisor}.{magnitude % divisor:0{number.decimals}d}{number.unit}"


def paint_number(canvas: Canvas, number: Widget, value: int | None = None) -> None:
    """Mirror ui::NumberPanel::Paint."""
    padding = 4
    style = number.style
    current = number.value if value is None else value
    if style.fill is not None:
        canvas.fill_rect(number.x, number.y, number.width, number.height, rgb565(style.fill))
    caption_scale = max(style.text_scale // 2, 1)
    caption = 0
    if number.text:
        canvas.draw_text(number.x + padding, number.y + padding, number.text, caption_scale,
                         rgb565(style.text))
        caption = GLYPH_HEIGHT * caption_scale + 2 * padding
    text = format_number(number, current)
    tw = text_width(text, style.text_scale)
    th = GLYPH_HEIGHT * style.text_scale
    inner = number.width - 2 * padding if number.width > 2 * padding else 0
    x = number.x + padding
    if tw < inner:
        if style.align == "center":
            x += (inner - tw) // 2
        elif style.align == "right":
            x += inner - tw
    area_top = number.y + caption
    area_height = number.height - caption if number.height > caption else 0
    y = area_top + (area_height - th) // 2 if th < area_height else area_top
    canvas.draw_text(x, y, text, style.text_scale, rgb565(style.text))


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
