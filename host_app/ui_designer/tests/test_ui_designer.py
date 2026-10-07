"""Unit tests: python3 -m unittest discover -s host_app/ui_designer/tests -t host_app"""

from __future__ import annotations

import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

from ui_designer import emit_cpp, font, render, schema

REPO_ROOT = Path(__file__).resolve().parents[3]


def _button(**overrides):
    widget = {
        "type": "button", "id": "toggle_boxes", "label": "BOXES",
        "x": 624, "y": 392, "width": 160, "height": 72,
    }
    widget.update(overrides)
    return widget


def _label(**overrides):
    label = {"type": "label", "id": "status", "text": "HELLO",
             "x": 0, "y": 0, "width": 800, "height": 24}
    label.update(overrides)
    return label


def _slider(**overrides):
    slider = {"type": "slider", "id": "level", "label": "LEVEL",
              "x": 40, "y": 100, "width": 720, "height": 72,
              "min": 0, "max": 100, "step": 5, "value": 50}
    slider.update(overrides)
    return slider


def _document(*widgets, screens=None, **overrides):
    """Two-screen document; `widgets` go on the camera screen `main`."""
    if screens is None:
        screens = [
            {"id": "main", "background": "camera",
             "widgets": list(widgets) or [_button()]},
            {"id": "menu", "background": "#101820", "widgets": []},
        ]
    document = {
        "schema_version": 2,
        "screen": {"width": 800, "height": 480},
        "namespace": "uai::ai::app_ui",
        "screens": screens,
    }
    document.update(overrides)
    return document


def _legacy_document(widget=None):
    return {
        "schema_version": 1,
        "screen": {"width": 800, "height": 480},
        "namespace": "uai::ai::app_ui",
        "widgets": [widget or _button()],
    }


class SchemaTest(unittest.TestCase):
    def test_defaults_and_round_trip(self):
        layout = schema.parse_layout(_document())
        button = layout.buttons()[0]
        self.assertEqual(button.style.text_scale, 3)
        self.assertEqual(button.style.fill, "#2060C0")
        self.assertEqual(button.icon, "none")
        self.assertEqual([s.id for s in layout.screens], ["main", "menu"])
        self.assertTrue(layout.screens[0].is_camera)
        self.assertFalse(layout.screens[1].is_camera)
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

    def test_legacy_single_screen_document_is_upgraded(self):
        layout = schema.parse_layout(_legacy_document())
        self.assertEqual(len(layout.screens), 1)
        self.assertEqual(layout.screens[0].id, "main")
        self.assertTrue(layout.screens[0].is_camera)
        self.assertEqual(layout.to_document()["schema_version"], 2)
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout({**_legacy_document(), "screens": []})

    def test_rejects_out_of_screen_widget(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(x=700, width=200)))

    def test_rejects_bad_identifier_and_duplicates_across_screens(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(id="ToggleBoxes")))
        document = _document()
        document["screens"][1]["widgets"].append(_button(y=16))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(document)
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(screens=[
                {"id": "main", "widgets": []}, {"id": "main", "widgets": []}]))

    def test_rejects_label_without_glyph(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(label="ボタン")))
        schema.parse_layout(_document(_button(label="boxes 1/2")))

    def test_overlap_is_per_screen(self):
        document = _document(_button(), _button(id="second", x=700, width=80))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(document)
        document = _document()
        document["screens"][1]["widgets"].append(_button(id="second"))
        schema.parse_layout(document)  # same bounds on another screen is fine

    def test_callbacks_are_cpp_identifiers_and_round_trip(self):
        layout = schema.parse_layout(_document(_button(on_tap="OnBoxes")))
        self.assertEqual(layout.widgets[0].on_tap, "OnBoxes")
        self.assertEqual(layout.widgets[0].on_press, "")
        document = layout.to_document()["screens"][0]["widgets"][0]
        self.assertEqual(document["on_tap"], "OnBoxes")
        self.assertNotIn("on_press", document)
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(on_tap="on-boxes")))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(on_tab="OnBoxes")))

    def test_rgb565_matches_device_formula(self):
        self.assertEqual(schema.rgb565("#FFFFFF"), 0xFFFF)
        self.assertEqual(schema.rgb565("#FF0000"), 0xF800)
        self.assertEqual(schema.rgb565("#00FF00"), 0x07E0)
        self.assertEqual(schema.rgb565("#0000FF"), 0x001F)

    def test_label_widget_defaults_and_constraints(self):
        layout = schema.parse_layout(_document(_label(style={"fill": None, "align": "right"})))
        label = layout.labels()[0]
        self.assertEqual(label.text, "HELLO")
        self.assertIsNone(label.style.fill)
        self.assertEqual(label.style.align, "right")
        self.assertEqual(label.style.text_scale, 2)
        self.assertEqual(label.style.padding, 4)
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

        for bad in (
            _label(on_tap="OnX"),
            _label(label="X"),
            _label(icon="menu"),
            _label(style={"align": "middle"}),
            _label(style={"border": "#FFFFFF"}),
            _label(text="A" * schema.LABEL_TEXT_CAPACITY),
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(bad))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(text="X")))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_label(type="knob")))

    def test_slider_widget_defaults_and_constraints(self):
        layout = schema.parse_layout(_document(_slider(on_change="OnLevel")))
        slider = layout.sliders()[0]
        self.assertEqual((slider.minimum, slider.maximum, slider.step, slider.value), (0, 100, 5, 50))
        self.assertEqual(slider.on_change, "OnLevel")
        self.assertTrue(slider.style.show_value)
        self.assertEqual(slider.style.track, "#404040")
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

        for bad in (
            _slider(min=10, max=10),
            _slider(step=0),
            _slider(step=500),
            _slider(value=101),
            _slider(on_tap="OnX"),
            _slider(icon="menu"),
            _slider(height=8),
            _slider(style={"show_value": "yes"}),
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(bad))

    def test_icon_and_navigate(self):
        layout = schema.parse_layout(_document(_button(icon="menu", navigate="menu", label="")))
        button = layout.buttons()[0]
        self.assertEqual(button.icon, "menu")
        self.assertEqual(button.navigate, "menu")
        document = layout.to_document()["screens"][0]["widgets"][0]
        self.assertEqual(document["icon"], "menu")
        self.assertEqual(document["navigate"], "menu")
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(icon="hamburger")))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(navigate="settings")))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(screens=[
                {"id": "main", "background": "red", "widgets": []}]))


class FontTest(unittest.TestCase):
    def test_table_matches_canvas_cpp(self):
        device = font.load_device_font(font.device_font_path(REPO_ROOT))
        self.assertGreater(len(device), 30)
        self.assertEqual(device, font.GLYPHS)

    def test_text_width_matches_cpp_formula(self):
        self.assertEqual(font.text_width("AB", 3), (2 * 6 - 1) * 3)
        self.assertEqual(font.text_width("", 3), 0)


class RenderTest(unittest.TestCase):
    def test_glyph_i_pixels(self):
        canvas = render.Canvas(32, 16)
        canvas.draw_text(1, 1, "I", 2, 0x07E0)
        row = lambda y: [canvas.pixels[y * 32 + x] for x in range(1, 11)]
        self.assertEqual(row(1), [0x07E0] * 10)
        self.assertEqual(row(14), [0x07E0] * 10)
        self.assertEqual(canvas.pixels[7 * 32 + 5], 0x07E0)
        self.assertEqual(canvas.pixels[7 * 32 + 1], 0)
        self.assertEqual(canvas.pixels[1 * 32 + 11], 0)

    def test_button_paint_states(self):
        layout = schema.parse_layout(_document())
        button = layout.widgets[0]
        normal = render.render_layout(layout)
        pressed = render.render_layout(layout, frozenset({"toggle_boxes"}))
        checked = render.render_layout(layout, checked_ids=frozenset({"toggle_boxes"}))
        center = (button.y + 10) * 800 + button.x + 10
        corner = button.y * 800 + button.x
        self.assertEqual(normal.pixels[center], schema.rgb565("#2060C0"))
        self.assertEqual(pressed.pixels[center], schema.rgb565("#103060"))
        self.assertEqual(checked.pixels[center], schema.rgb565("#00A060"))
        self.assertEqual(normal.pixels[corner], 0xFFFF)
        self.assertEqual(normal.pixels[(button.y - 1) * 800 + button.x], 0x4208)

    def test_icon_matches_device_geometry(self):
        """Same case as UiButtonPanel.IconReplacesLabel in ui_test.cpp."""
        canvas = render.Canvas(16, 16, 0x0001)
        layout = schema.parse_layout(_document(
            _button(id="m", x=0, y=0, width=16, height=16, icon="menu", label="X",
                    style={"fill": "#000000", "border_width": 0, "text": "#FFFFFF"})))
        render.paint_button(canvas, layout.buttons()[0])
        self.assertEqual(canvas.pixels[4 * 16 + 4], 0xFFFF)
        self.assertEqual(canvas.pixels[4 * 16 + 11], 0xFFFF)
        self.assertEqual(canvas.pixels[11 * 16 + 4], 0xFFFF)
        self.assertEqual(canvas.pixels[5 * 16 + 4], 0x0000)
        self.assertEqual(canvas.pixels[4 * 16 + 3], 0x0000)

    def test_slider_track_matches_device_geometry(self):
        """Same case as UiSlider in ui_test.cpp: 32 px wide, 1x caption."""
        layout = schema.parse_layout(_document(
            _slider(id="s", x=0, y=0, width=32, height=19, min=0, max=30, step=10, value=30,
                    style={"text_scale": 1, "show_value": False, "track": "#101010",
                           "fill": "#202020", "knob": "#303030"})))
        slider = layout.sliders()[0]
        tx, ty, tw, th = render.slider_track(slider)
        self.assertEqual((tx, tw, th), (8, 16, 8))
        canvas = render.Canvas(32, 32)
        render.paint_slider(canvas, slider)
        self.assertEqual(canvas.pixels[ty * 32 + tx], schema.rgb565("#202020"))
        self.assertEqual(canvas.pixels[ty * 32 + tx + tw - 1], schema.rgb565("#303030"))
        render.paint_slider(canvas, slider, value=0)
        self.assertEqual(canvas.pixels[ty * 32 + tx + tw - 1], schema.rgb565("#101010"))
        self.assertEqual(canvas.pixels[ty * 32 + tx], schema.rgb565("#303030"))

    def test_label_paint_alignment_and_transparency(self):
        document = _document(
            _label(id="a", text="I", x=0, y=0, width=32, height=9,
                   style={"text_scale": 1, "align": "right", "padding": 4, "fill": None}),
            _label(id="b", text="", x=0, y=20, width=16, height=8, style={"fill": "#FF0000"}),
        )
        canvas = render.render_layout(schema.parse_layout(document))
        self.assertEqual(canvas.pixels[1 * 800 + 27], 0xFFFF)
        self.assertEqual(canvas.pixels[1 * 800 + 28], 0x4208)
        self.assertEqual(canvas.pixels[0], 0x4208)  # transparent background
        self.assertEqual(canvas.pixels[20 * 800 + 3], 0xF800)

    def test_solid_screen_background(self):
        layout = schema.parse_layout(_document())
        menu = render.render_layout(layout, screen_id="menu")
        self.assertEqual(menu.pixels[0], schema.rgb565("#101820"))
        with self.assertRaises(schema.LayoutError):
            render.render_layout(layout, screen_id="nope")

    def test_png_header(self):
        data = render.encode_png(render.Canvas(4, 2, 0xF800))
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertIn(b"IHDR", data)
        self.assertTrue(data.endswith(b"IEND\xaeB`\x82"))


class EmitTest(unittest.TestCase):
    def test_header_contents(self):
        layout = schema.parse_layout(_document())
        text = emit_cpp.generate_header(layout, "ui_layout.json")
        self.assertIn("#pragma once", text)
        self.assertIn("kMain = 0U,", text)
        self.assertIn("kMenu = 1U,", text)
        self.assertIn("kToggleBoxes = 1U,", text)
        self.assertIn("inline constexpr ui::ButtonSpec kMainButtons[] = {", text)
        self.assertIn('"BOXES"', text)
        self.assertIn("{624U, 392U, 160U, 72U}", text)
        self.assertIn("ui::Rgb565(0x00U, 0xA0U, 0x60U)", text)  # checked_fill
        self.assertIn("ui::Icon::kNone,", text)
        self.assertIn("ui::Background::kCamera, 0U,", text)
        self.assertIn("ui::Background::kSolid, ui::Rgb565(0x10U, 0x18U, 0x20U),", text)
        self.assertIn("kMainButtons, sizeof(kMainButtons) / sizeof(kMainButtons[0]),", text)
        self.assertIn("inline constexpr std::size_t kScreenCount =", text)
        self.assertNotIn("kMenuButtons", text)

    def test_empty_layout_header(self):
        layout = schema.parse_layout(_document(screens=[{"id": "main", "widgets": []}]))
        text = emit_cpp.generate_header(layout, "x.json")
        self.assertIn("enum class WidgetId : std::uint16_t {\n};", text)
        self.assertIn("        nullptr, 0U,\n        nullptr, 0U,\n        nullptr, 0U,", text)
        self.assertIn("(void)handlers;", text)

    def test_label_and_slider_emission(self):
        text = emit_cpp.generate_header(schema.parse_layout(_document(
            _button(), _label(style={"fill": None, "align": "center", "padding": 0}),
            _slider(on_change="OnLevel"))), "x.json")
        self.assertIn("inline constexpr ui::LabelSpec kMainLabels[] = {", text)
        self.assertIn("ui::TextAlign::kCenter,", text)
        self.assertIn("inline constexpr ui::SliderSpec kMainSliders[] = {", text)
        self.assertIn("        0, 100, 5, 50,", text)
        self.assertIn("            true,\n        },", text)
        self.assertIn("if (event.type == ui::EventType::kChange)", text)
        self.assertIn("handlers.OnLevel(event);", text)
        self.assertIn("void OnLevel(const ui::Event &event);", text)

    def test_dispatch_routes_navigation_and_callbacks(self):
        document = _document(
            _button(on_tap="OnBoxes", on_press="OnBoxesDown"),
            _button(id="go_menu", x=16, y=16, width=56, height=48, icon="menu", navigate="menu"),
            _button(id="both", x=100, y=16, width=56, height=48, navigate="menu", on_tap="OnBoth"))
        text = emit_cpp.generate_header(schema.parse_layout(document), "x.json")
        self.assertIn("void ShowScreen(ScreenId screen);", text)
        tap = text.index("ui::EventType::kTap")
        press = text.index("ui::EventType::kPress")
        tap_block = text[tap:press]
        self.assertIn("case WidgetId::kGoMenu:\n            handlers.ShowScreen(ScreenId::kMenu);", tap_block)
        self.assertIn("handlers.OnBoth(event);\n            handlers.ShowScreen(ScreenId::kMenu);", tap_block)
        self.assertIn("handlers.OnBoxes(event);", tap_block)
        self.assertIn("handlers.OnBoxesDown(event);", text[press:])
        self.assertNotIn("kGoMenu", text[press:])

    def test_checked_in_header_is_current(self):
        layout_path = REPO_ROOT / "userspace/ai-app/config/ui_layout.json"
        header_path = REPO_ROOT / "userspace/ai-app/src/ui/ui_layout.hpp"
        expected = emit_cpp.generate_header(schema.load_layout(layout_path), layout_path.name)
        self.assertEqual(header_path.read_text(encoding="utf-8"), expected)


class CliTest(unittest.TestCase):
    def test_generate_and_check(self):
        from ui_designer.cli import main

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            layout.write_text(json.dumps(_document()), encoding="utf-8")
            header = Path(tmp, "out", "ui_layout.hpp")
            self.assertEqual(main(["generate", "--layout", str(layout), "--output", str(header)]), 0)
            self.assertEqual(main(["generate", "--layout", str(layout), "--output", str(header), "--check"]), 0)
            header.write_text("stale", encoding="utf-8")
            self.assertEqual(main(["generate", "--layout", str(layout), "--output", str(header), "--check"]), 1)
            png = Path(tmp, "preview.png")
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png)]), 0)
            self.assertTrue(png.stat().st_size > 100)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--screen", "menu"]), 0)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--screen", "x"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--pressed", "nope"]), 2)

    def test_edit_commands(self):
        from ui_designer.cli import main

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            header = Path(tmp, "ui_layout.hpp")
            png = Path(tmp, "edit.png")
            self.assertEqual(main(["init", "--layout", str(layout)]), 0)
            self.assertEqual(main(["init", "--layout", str(layout)]), 2)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "start", "--x", "16", "--y", "392",
                "--width", "160", "--height", "72", "--on-tap", "OnStart",
                "--fill", "#00AA00", "--header", str(header)]), 0)
            doc = json.loads(layout.read_text())
            widget = doc["screens"][0]["widgets"][0]
            self.assertEqual(widget["label"], "START")
            self.assertEqual(widget["on_tap"], "OnStart")
            self.assertEqual(widget["style"]["fill"], "#00AA00")
            self.assertEqual(widget["style"]["text_scale"], 3)
            self.assertIn("handlers.OnStart(event);", header.read_text())

            # Duplicate id and an edit that leaves the screen are rejected and
            # leave the file untouched.
            before = layout.read_text()
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "start", "--x", "0", "--y", "0",
                "--width", "8", "--height", "8"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "start", "--x", "700"]), 2)
            self.assertEqual(layout.read_text(), before)

            self.assertEqual(main([
                "set", "--layout", str(layout), "--id", "start", "--rename", "go",
                "--label", "GO", "--on-tap", "", "--on-press", "OnGoDown",
                "--header", str(header)]), 0)
            doc = json.loads(layout.read_text())
            widget = doc["screens"][0]["widgets"][0]
            self.assertEqual(widget["id"], "go")
            self.assertNotIn("on_tap", widget)
            self.assertEqual(widget["on_press"], "OnGoDown")
            self.assertIn("kGo = 1U", header.read_text())

            # Screens: add a solid menu, navigate to it, move a slider there.
            self.assertEqual(main(["screen-add", "--layout", str(layout), "--id", "menu",
                                   "--background", "#101820"]), 0)
            self.assertEqual(main(["screen-add", "--layout", str(layout), "--id", "menu"]), 2)
            self.assertEqual(main(["screen-add", "--layout", str(layout), "--id", "bad",
                                   "--background", "blue"]), 2)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "open", "--x", "744", "--y", "0",
                "--width", "56", "--height", "48", "--icon", "menu", "--navigate", "menu"]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "open2", "--x", "600", "--y", "0",
                "--width", "56", "--height", "48", "--navigate", "nowhere"]), 2)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "slider", "--id", "level",
                "--screen", "menu", "--x", "40", "--y", "100", "--width", "720", "--height", "72",
                "--min", "0", "--max", "10", "--step", "2", "--value", "4",
                "--on-change", "OnLevel", "--show-value", "false", "--header", str(header)]), 0)
            doc = json.loads(layout.read_text())
            menu = next(s for s in doc["screens"] if s["id"] == "menu")
            slider = menu["widgets"][0]
            self.assertEqual(slider["type"], "slider")
            self.assertEqual((slider["min"], slider["max"], slider["step"], slider["value"]), (0, 10, 2, 4))
            self.assertFalse(slider["style"]["show_value"])
            text = header.read_text()
            self.assertIn("handlers.ShowScreen(ScreenId::kMenu);", text)
            self.assertIn("handlers.OnLevel(event);", text)
            # Button-only / slider-only options are rejected for the wrong kind.
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "level", "--icon", "back"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "go", "--min", "1"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "level", "--value", "11"]), 2)
            # Move the slider to main (no overlap there), then back.
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "level", "--screen", "main"]), 0)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "level", "--screen", "menu"]), 0)
            # Renaming a screen updates navigate references.
            self.assertEqual(main(["screen-set", "--layout", str(layout), "--id", "menu",
                                   "--rename", "settings", "--background", "#000000"]), 0)
            doc = json.loads(layout.read_text())
            self.assertEqual(doc["screens"][1]["id"], "settings")
            self.assertEqual(doc["screens"][1]["background"], "#000000")
            self.assertEqual(doc["screens"][0]["widgets"][1]["navigate"], "settings")
            self.assertEqual(main(["screen-remove", "--layout", str(layout), "--id", "settings"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png),
                                   "--screen", "settings"]), 0)
            self.assertEqual(main(["screen-remove", "--layout", str(layout), "--id", "settings", "--force"]), 2)
            # Removing the target leaves a dangling navigate, so it is refused;
            # clear it first.
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "open", "--navigate", ""]), 0)
            self.assertEqual(main(["screen-remove", "--layout", str(layout), "--id", "settings", "--force"]), 0)
            self.assertEqual(len(json.loads(layout.read_text())["screens"]), 1)

            # Labels: --text, --align, transparent fill; button-only options rejected.
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "label", "--id", "fps_label",
                "--x", "0", "--y", "100", "--width", "300", "--height", "24",
                "--align", "center", "--fill", "none", "--text-color", "#FFE000"]), 0)
            doc = json.loads(layout.read_text())
            fps = next(w for w in doc["screens"][0]["widgets"] if w["id"] == "fps_label")
            self.assertEqual(fps["text"], "FPS LABEL")
            self.assertIsNone(fps["style"]["fill"])
            self.assertEqual(fps["style"]["text"], "#FFE000")
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--text", "12.5 FPS"]), 0)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--on-tap", "OnX"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--border-width", "1"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "go", "--text", "X"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--checked", "fps_label"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--checked", "go"]), 0)

            self.assertEqual(main(["screen", "--layout", str(layout), "--namespace", "demo::ui"]), 0)
            self.assertEqual(main(["screen", "--layout", str(layout), "--width", "100"]), 2)
            self.assertEqual(main(["list", "--layout", str(layout)]), 0)
            self.assertEqual(main(["remove", "--layout", str(layout), "--id", "go"]), 0)
            self.assertEqual(main(["remove", "--layout", str(layout), "--id", "go"]), 2)

    def test_legacy_file_is_upgraded_on_edit(self):
        from ui_designer.cli import main

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            layout.write_text(json.dumps(_legacy_document()), encoding="utf-8")
            self.assertEqual(main(["screen-add", "--layout", str(layout), "--id", "menu"]), 0)
            doc = json.loads(layout.read_text())
            self.assertEqual(doc["schema_version"], 2)
            self.assertEqual([s["id"] for s in doc["screens"]], ["main", "menu"])
            self.assertNotIn("widgets", doc)


class ServerTest(unittest.TestCase):
    def test_writes_require_loopback_host_and_session_token(self):
        from ui_designer.server import EditorState, _make_handler

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            layout.write_text(json.dumps(_document()), encoding="utf-8")
            server = ThreadingHTTPServer(("127.0.0.1", 0), _make_handler(EditorState(layout)))
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                url = f"http://127.0.0.1:{server.server_address[1]}"
                with urllib.request.urlopen(url + "/api/layout") as response:
                    token = json.load(response)["token"]
                body = json.dumps(_document(_button(x=16))).encode()
                for headers, status in (
                    ({}, 403),
                    ({"X-Editor-Token": token, "Host": "evil.example"}, 403),
                ):
                    request = urllib.request.Request(url + "/api/layout", data=body, headers=headers)
                    with self.assertRaises(urllib.error.HTTPError) as caught:
                        urllib.request.urlopen(request)
                    self.assertEqual(caught.exception.code, status)
                    caught.exception.close()
                self.assertEqual(json.loads(layout.read_text())["screens"][0]["widgets"][0]["x"], 624)

                request = urllib.request.Request(
                    url + "/api/layout", data=body, headers={"X-Editor-Token": token})
                with urllib.request.urlopen(request) as response:
                    self.assertTrue(json.load(response)["ok"])
                self.assertEqual(json.loads(layout.read_text())["screens"][0]["widgets"][0]["x"], 16)
                with urllib.request.urlopen(url + "/api/preview.png?screen=menu") as response:
                    self.assertEqual(response.headers["Content-Type"], "image/png")
            finally:
                server.shutdown()
                server.server_close()
                worker.join()


if __name__ == "__main__":
    unittest.main()
