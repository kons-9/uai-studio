"""Emit the C++ header consumed by kernel/middleware/ui from a Layout."""

from __future__ import annotations

from pathlib import Path

from .images import Bitmap, load_bitmap
from .schema import Layout, Screen, Widget, parse_color, pascal_case


def _rgb(color: str) -> str:
    red, green, blue = parse_color(color)
    return f"ui::Rgb565(0x{red:02X}U, 0x{green:02X}U, 0x{blue:02X}U)"


def _cpp_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _widget_id(widget: Widget) -> str:
    return f"static_cast<std::uint16_t>(WidgetId::k{pascal_case(widget.id)})"


def _bounds(widget: Widget) -> str:
    return f"{{{widget.x}U, {widget.y}U, {widget.width}U, {widget.height}U}}"


def _shape(name: str) -> str:
    return "ui::Shape::k" + pascal_case(name)


def _button_entry(widget: Widget) -> list[str]:
    style = widget.style
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {_cpp_string(widget.text)},",
        "        {",
        f"            {_rgb(style.fill)},",
        f"            {_rgb(style.pressed_fill)},",
        f"            {_rgb(style.checked_fill)},",
        f"            {_rgb(style.border)},",
        f"            {_rgb(style.text)},",
        f"            {style.text_scale}U,",
        f"            {style.border_width}U,",
        "        },",
        f"        ui::Icon::k{widget.icon.capitalize()},",
        f"        {_shape(widget.shape)},",
        "    },",
    ]


def _label_entry(widget: Widget) -> list[str]:
    style = widget.style
    fill = _rgb(style.fill) if style.fill is not None else "0U"
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {_cpp_string(widget.text)},",
        "        {",
        f"            {_rgb(style.text)},",
        f"            {fill},",
        f"            {'true' if style.fill is not None else 'false'},",
        f"            {style.text_scale}U,",
        f"            ui::TextAlign::k{style.align.capitalize()},",
        f"            {style.padding}U,",
        "        },",
        "    },",
    ]


def _slider_entry(widget: Widget) -> list[str]:
    style = widget.style
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {_cpp_string(widget.text)},",
        f"        {widget.minimum}, {widget.maximum}, {widget.step}, {widget.value},",
        "        {",
        f"            {_rgb(style.track)},",
        f"            {_rgb(style.fill)},",
        f"            {_rgb(style.knob)},",
        f"            {_rgb(style.text)},",
        f"            {style.text_scale}U,",
        f"            {'true' if style.show_value else 'false'},",
        "        },",
        "    },",
    ]


def _table(name: str, type_name: str, widgets: list[Widget], entry) -> list[str]:
    if not widgets:
        return []
    lines = [f"inline constexpr ui::{type_name} {name}[] = {{"]
    for widget in widgets:
        lines += entry(widget)
    lines += ["};", ""]
    return lines


def _dial_entry(widget: Widget) -> list[str]:
    style = widget.style
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {_cpp_string(widget.text)},",
        f"        {widget.minimum}, {widget.maximum}, {widget.step}, {widget.value},",
        "        {",
        f"            {_rgb(style.face)},",
        f"            {_rgb(style.track)},",
        f"            {_rgb(style.fill)},",
        f"            {_rgb(style.pointer)},",
        f"            {_rgb(style.text)},",
        f"            {style.text_scale}U,",
        f"            {'true' if style.show_value else 'false'},",
        "        },",
        "    },",
    ]


def _wheel_items_name(widget: Widget) -> str:
    return f"k{pascal_case(widget.id)}Items"


def _wheel_items(widget: Widget) -> list[str]:
    lines = [f"inline constexpr const char *const {_wheel_items_name(widget)}[] = {{"]
    lines += [f"    {_cpp_string(item)}," for item in widget.items]
    lines += ["};"]
    return lines


def _wheel_entry(widget: Widget) -> list[str]:
    style = widget.style
    items = _wheel_items_name(widget)
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {items}, sizeof({items}) / sizeof({items}[0]), {widget.value}U,",
        "        {",
        f"            {_rgb(style.fill)},",
        f"            {_rgb(style.highlight)},",
        f"            {_rgb(style.text)},",
        f"            {_rgb(style.selected_text)},",
        f"            {_rgb(style.border)},",
        f"            {style.text_scale}U,",
        "        },",
        "    },",
    ]


def _number_entry(widget: Widget) -> list[str]:
    style = widget.style
    fill = _rgb(style.fill) if style.fill is not None else "0U"
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {_cpp_string(widget.text)},",
        f"        {_cpp_string(widget.unit)},",
        f"        {widget.decimals}U,",
        f"        {widget.value},",
        "        {",
        f"            {_rgb(style.text)},",
        f"            {fill},",
        f"            {'true' if style.fill is not None else 'false'},",
        f"            {style.text_scale}U,",
        f"            ui::TextAlign::k{style.align.capitalize()},",
        "        },",
        "    },",
    ]


def _image_pixels_name(widget: Widget) -> str:
    return f"k{pascal_case(widget.id)}Pixels"


def _image_content(bitmap: Bitmap) -> tuple[int, int, Bitmap]:
    key = bitmap.transparent
    if key is None:
        return 0, 0, bitmap
    left, top = bitmap.width, bitmap.height
    right = bottom = -1
    for row in range(bitmap.height):
        for column in range(bitmap.width):
            if bitmap.pixels[row * bitmap.width + column] == key:
                continue
            left = min(left, column)
            right = max(right, column)
            top = min(top, row)
            bottom = max(bottom, row)
    if right < left:
        return 0, 0, Bitmap(1, 1, [key], key)
    width, height = right - left + 1, bottom - top + 1
    pixels = []
    for row in range(top, bottom + 1):
        start = row * bitmap.width + left
        pixels.extend(bitmap.pixels[start:start + width])
    return left, top, Bitmap(width, height, pixels, key)


def _image_entry(bitmaps: dict[str, Bitmap]):
    def entry(widget: Widget) -> list[str]:
        left, top, bitmap = _image_content(bitmaps[widget.id])
        transparent = (f"true, 0x{bitmap.transparent:04X}U" if bitmap.transparent is not None
                       else "false, 0U")
        return [
            "    {",
            f"        {_widget_id(widget)},",
            f"        {{{widget.x + left}U, {widget.y + top}U, {bitmap.width}U, {bitmap.height}U}},",
            f"        {_image_pixels_name(widget)},",
            f"        {transparent},",
            "    },",
        ]
    return entry


def _pad_entry(widget: Widget) -> list[str]:
    style = widget.style
    return [
        "    {",
        f"        {_widget_id(widget)},",
        f"        {_bounds(widget)},",
        f"        {'true' if widget.center else 'false'},",
        "        {",
        f"            {_rgb(style.fill)},",
        f"            {_rgb(style.pressed_fill)},",
        f"            {_rgb(style.center_fill)},",
        f"            {_rgb(style.border)},",
        f"            {_rgb(style.arrow)},",
        f"            {style.border_width}U,",
        "        },",
        "    },",
    ]


def _screen_tables(screen: Screen, bitmaps: dict[str, Bitmap]) -> list[str]:
    prefix = f"k{pascal_case(screen.id)}"
    lines: list[str] = []
    for wheel in screen.wheels():
        lines += _wheel_items(wheel)
    if screen.wheels():
        lines.append("")
    return (lines +
            _table(f"{prefix}Buttons", "ButtonSpec", screen.buttons(), _button_entry) +
            _table(f"{prefix}Labels", "LabelSpec", screen.labels(), _label_entry) +
            _table(f"{prefix}Sliders", "SliderSpec", screen.sliders(), _slider_entry) +
            _table(f"{prefix}Dials", "DialSpec", screen.dials(), _dial_entry) +
            _table(f"{prefix}Wheels", "WheelSpec", screen.wheels(), _wheel_entry) +
            _table(f"{prefix}Numbers", "NumberSpec", screen.numbers(), _number_entry) +
            _table(f"{prefix}Images", "ImageSpec", screen.images(), _image_entry(bitmaps)) +
            _table(f"{prefix}Pads", "PadSpec", screen.pads(), _pad_entry))


def _screen_entry(screen: Screen) -> list[str]:
    prefix = f"k{pascal_case(screen.id)}"

    def table(kind: str, widgets: list[Widget]) -> list[str]:
        if not widgets:
            return ["        nullptr, 0U,"]
        name = f"{prefix}{kind}"
        return [f"        {name}, sizeof({name}) / sizeof({name}[0]),"]

    background = ("ui::Background::kCamera, 0U" if screen.is_camera
                  else f"ui::Background::kSolid, {_rgb(screen.background)}")
    return (["    {",
             f"        static_cast<std::uint16_t>(ScreenId::k{pascal_case(screen.id)}),",
             f"        {background},"]
            + table("Buttons", screen.buttons())
            + table("Labels", screen.labels())
            + table("Sliders", screen.sliders())
            + table("Dials", screen.dials())
            + table("Wheels", screen.wheels())
            + table("Numbers", screen.numbers())
            + table("Images", screen.images())
            + table("Pads", screen.pads())
            + ["    },"])


def images_header_name(output: Path) -> Path:
    """Bitmaps go next to the layout header so it stays readable."""
    return output.with_name(output.stem + "_images.hpp")


def load_bitmaps(layout: Layout, layout_path: Path) -> dict[str, Bitmap]:
    return {widget.id: load_bitmap(layout_path, widget) for widget in layout.images()}


IMAGE_SECTION = ".ui_assets"


def generate_images_header(layout: Layout, bitmaps: dict[str, Bitmap],
                           source_name: str) -> str:
    lines = [
        f"// Generated by host_app/ui_designer from {source_name}. Do not edit.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        f"namespace {layout.namespace} {{",
    ]
    for widget in layout.images():
        _, _, bitmap = _image_content(bitmaps[widget.id])
        lines += [
            "",
            f"/* {widget.source}: {bitmap.width}x{bitmap.height} RGB565 */",
            # Kept out of .rodata so large bitmaps cannot push it into the
            # fixed-address launch entry; the linker script places .ui_assets.
            f'[[gnu::section("{IMAGE_SECTION}")]]',
            f"inline constexpr std::uint16_t {_image_pixels_name(widget)}[] = {{",
        ]
        for row in range(bitmap.height):
            values = bitmap.pixels[row * bitmap.width:(row + 1) * bitmap.width]
            for start in range(0, len(values), 12):
                lines.append("    " + ", ".join(f"0x{v:04X}U" for v in values[start:start + 12]) + ",")
        lines.append("};")
    lines += ["", f"}} // namespace {layout.namespace}", ""]
    return "\n".join(lines)


def generate_header(layout: Layout, source_name: str,
                    bitmaps: dict[str, Bitmap] | None = None,
                    images_include: str | None = None) -> str:
    """Emit the layout header. `bitmaps` is required when the layout has
    images; `images_include` is the file name of the bitmap header."""
    if layout.images() and (bitmaps is None or images_include is None):
        raise ValueError("layouts with images need bitmaps and an images include")
    lines = [
        f"// Generated by host_app/ui_designer from {source_name}. Do not edit.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        '#include "middleware/ui/widget.hpp"',
    ]
    if layout.images():
        lines.append(f'#include "{images_include}"')
    lines += [
        "",
        f"namespace {layout.namespace} {{",
        "",
        f"inline constexpr std::uint16_t kScreenWidth = {layout.width}U;",
        f"inline constexpr std::uint16_t kScreenHeight = {layout.height}U;",
        "",
        "enum class ScreenId : std::uint16_t {",
    ]
    for index, screen in enumerate(layout.screens):
        lines.append(f"    k{pascal_case(screen.id)} = {index}U,")
    lines += ["};", "", "enum class WidgetId : std::uint16_t {"]
    for index, widget in enumerate(layout.widgets, start=1):
        lines.append(f"    k{pascal_case(widget.id)} = {index}U,")
    lines += ["};", ""]

    for screen in layout.screens:
        lines += _screen_tables(screen, bitmaps or {})

    lines.append("/* Indexed by ScreenId. */")
    lines.append("inline constexpr ui::ScreenSpec kScreens[] = {")
    for screen in layout.screens:
        lines += _screen_entry(screen)
    lines += [
        "};",
        "inline constexpr std::size_t kScreenCount =",
        "    sizeof(kScreens) / sizeof(kScreens[0]);",
    ]
    lines += _dispatch(layout)
    lines += [
        "",
        f"}} // namespace {layout.namespace}",
        "",
    ]
    return "\n".join(lines)


# (event key, ui::EventType member, widget filter)
_EVENT_TYPES = (
    ("on_tap", "kTap", lambda w: w.is_button or w.is_pad),
    ("on_press", "kPress", lambda w: w.is_button or w.is_pad),
    ("on_change", "kChange", lambda w: w.is_slider or w.is_dial or w.is_wheel or w.is_pad),
)


def _dispatch(layout: Layout) -> list[str]:
    """Route events to handler methods named in the layout.

    Handlers is any type with `void <name>(const ui::Event &)` methods and,
    when any button navigates, `void ShowScreen(ScreenId)`. A missing method
    is a compile error at the application's call site.
    """
    handlers = sorted({
        getattr(w, event) for w in layout.widgets for event, _, accepts in _EVENT_TYPES
        if accepts(w) and getattr(w, event)})
    navigates = any(w.navigate for w in layout.buttons())
    lines = [
        "",
        "/* Handler methods the application must provide on its Handlers type:",
    ]
    lines += [f" *   void {name}(const ui::Event &event);" for name in handlers]
    if navigates:
        lines.append(" *   void ShowScreen(ScreenId screen);")
    if not handlers and not navigates:
        lines.append(" *   (none)")
    lines += [
        " */",
        "template <typename Handlers>",
        "bool Dispatch(Handlers &handlers, const ui::Event &event)",
        "{",
    ]
    cases_emitted = False
    for event, enum_name, accepts in _EVENT_TYPES:
        bound = [w for w in layout.widgets if accepts(w) and
                 (getattr(w, event) or (event == "on_tap" and w.navigate))]
        if not bound:
            continue
        cases_emitted = True
        lines += [
            f"    if (event.type == ui::EventType::{enum_name}) {{",
            "        switch (static_cast<WidgetId>(event.widget_id)) {",
        ]
        for widget in bound:
            lines.append(f"        case WidgetId::k{pascal_case(widget.id)}:")
            handler = getattr(widget, event)
            if handler:
                lines.append(f"            handlers.{handler}(event);")
            if event == "on_tap" and widget.navigate:
                lines.append(
                    f"            handlers.ShowScreen(ScreenId::k{pascal_case(widget.navigate)});")
            lines.append("            return true;")
        lines += [
            "        default:",
            "            break;",
            "        }",
            "    }",
        ]
    if not cases_emitted:
        lines += ["    (void)handlers;", "    (void)event;"]
    lines += ["    return false;", "}"]
    return lines
