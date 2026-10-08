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

from ui_designer import emit_cpp, font, images, png, render, schema

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


def _dial(**overrides):
    dial = {"type": "dial", "id": "period", "label": "MS",
            "x": 500, "y": 100, "width": 200, "height": 220,
            "min": 100, "max": 2000, "step": 100, "value": 500}
    dial.update(overrides)
    return dial


def _wheel(**overrides):
    wheel = {"type": "wheel", "id": "mode", "items": ["ALL", "PERSON", "FACE"],
             "value": 0, "x": 40, "y": 200, "width": 240, "height": 150}
    wheel.update(overrides)
    return wheel


def _number(**overrides):
    number = {"type": "number", "id": "count", "label": "DET", "unit": "%",
              "decimals": 1, "value": 1234, "x": 300, "y": 200, "width": 200, "height": 72}
    number.update(overrides)
    return number


def _image(**overrides):
    image = {"type": "image", "id": "logo", "source": "logo.png",
             "x": 700, "y": 16, "width": 8, "height": 4}
    image.update(overrides)
    return image


def _pad(**overrides):
    pad = {"type": "pad", "id": "nav", "x": 500, "y": 100, "width": 160, "height": 160}
    pad.update(overrides)
    return pad


def _write_test_png(path: Path) -> None:
    """4x2 RGBA: top row opaque red, bottom row transparent except (3, 1) blue."""
    pixels = [(255, 0, 0, 255)] * 4 + [(0, 0, 0, 0)] * 3 + [(0, 0, 255, 255)]
    png.write_png(path, 4, 2, pixels)


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

    def test_button_shape(self):
        layout = schema.parse_layout(_document(_button(shape="ellipse")))
        self.assertEqual(layout.buttons()[0].shape, "ellipse")
        self.assertEqual(layout.to_document()["screens"][0]["widgets"][0]["shape"], "ellipse")
        self.assertEqual(schema.parse_layout(_document(_button())).buttons()[0].shape, "rectangle")
        for shape in schema.SHAPES:
            schema.parse_layout(_document(_button(shape=shape)))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_button(shape="circle")))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(_label(shape="ellipse")))

    def test_pad_defaults_and_constraints(self):
        layout = schema.parse_layout(_document(_pad(on_tap="OnNav", on_change="OnTurn")))
        pad = layout.pads()[0]
        self.assertTrue(pad.center)
        self.assertEqual((pad.on_tap, pad.on_press, pad.on_change), ("OnNav", "", "OnTurn"))
        self.assertEqual(pad.style.arrow, schema.PAD_STYLE_DEFAULTS["arrow"])
        document = layout.to_document()["screens"][0]["widgets"][0]
        self.assertIs(document["center"], True)
        self.assertNotIn("label", document)
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)
        self.assertFalse(schema.parse_layout(_document(_pad(center=False))).pads()[0].center)
        for bad in (
            _pad(width=16, height=160),
            _pad(center="yes"),
            _pad(label="X"),
            _pad(shape="ellipse"),
            _pad(min=0),
            _button(center=True),
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(bad))
        pads = [_pad(id=f"nav_{index}", x=index * 200) for index in range(3)]
        with self.assertRaisesRegex(schema.LayoutError, "at most 2 pads"):
            schema.parse_layout(_document(*pads))
        valid = _document(*pads[:2])
        valid["screens"][1]["widgets"] = [pads[2]]
        self.assertEqual(len(schema.parse_layout(valid).pads()), 3)

    def test_dial_wheel_number_defaults_and_constraints(self):
        layout = schema.parse_layout(_document(
            _dial(on_change="OnPeriod"), _wheel(on_change="OnMode", value=2), _number()))
        dial, wheel, number = layout.dials()[0], layout.wheels()[0], layout.numbers()[0]
        self.assertEqual((dial.minimum, dial.maximum, dial.step, dial.value), (100, 2000, 100, 500))
        self.assertEqual(dial.style.pointer, schema.DIAL_STYLE_DEFAULTS["pointer"])
        self.assertEqual(wheel.items, ["ALL", "PERSON", "FACE"])
        self.assertEqual(wheel.value, 2)
        self.assertEqual((number.text, number.unit, number.decimals, number.value), ("DET", "%", 1, 1234))
        self.assertEqual(number.style.align, "right")
        self.assertEqual(number.style.text_scale, 4)
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

        for bad in (
            _dial(min=5, max=5),
            _dial(value=50),
            _dial(height=10),
            _dial(on_tap="OnX"),
            _wheel(items=[]),
            _wheel(items=["A"] * (schema.MAX_WHEEL_ITEMS + 1)),
            _wheel(value=3),
            _wheel(items=["ボタン"]),
            _wheel(min=0),
            _number(decimals=-1),
            _number(decimals=7),
            _number(on_change="OnX"),
            _number(unit="A" * 20),
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(bad))

    def test_image_source_is_relative_to_layout(self):
        layout = schema.parse_layout(_document(_image(transparent="#00FF00")))
        image = layout.images()[0]
        self.assertEqual(image.source, "logo.png")
        self.assertEqual(image.transparent, "#00FF00")
        document = layout.to_document()["screens"][0]["widgets"][0]
        self.assertEqual(document["source"], "logo.png")
        self.assertIsNone(schema.parse_layout(_document(_image())).images()[0].transparent)
        for bad in (
            _image(source=""),
            _image(source="/etc/passwd"),
            _image(source="../logo.png"),
            _image(on_tap="OnX"),
            _image(label="X"),
            _image(width=400, height=400),  # exceeds IMAGE_MAX_BYTES
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(bad))


class PngTest(unittest.TestCase):
    def test_write_and_read_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "t.png")
            _write_test_png(path)
            width, height, pixels = png.read_png(path)
        self.assertEqual((width, height), (4, 2))
        self.assertEqual(pixels[0], (255, 0, 0, 255))
        self.assertEqual(pixels[4], (0, 0, 0, 0))
        self.assertEqual(pixels[7], (0, 0, 255, 255))

    def test_rejects_non_png(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "t.png")
            path.write_bytes(b"not a png")
            with self.assertRaises(png.PngError):
                png.read_png(path)

    def test_load_bitmap_resamples_and_keys_transparency(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout_path = Path(tmp, "ui.json")
            _write_test_png(Path(tmp, "logo.png"))
            layout = schema.parse_layout(_document(_image(width=8, height=4)))
            bitmap = images.load_bitmap(layout_path, layout.images()[0])
            self.assertEqual((bitmap.width, bitmap.height), (8, 4))
            key = schema.rgb565(schema.DEFAULT_TRANSPARENT_KEY)
            self.assertEqual(bitmap.transparent, key)
            self.assertEqual(bitmap.pixels[0], 0xF800)          # red, 2x upscaled
            self.assertEqual(bitmap.pixels[1 * 8 + 7], 0xF800)
            self.assertEqual(bitmap.pixels[2 * 8 + 0], key)     # alpha 0 -> key
            self.assertEqual(bitmap.pixels[3 * 8 + 7], 0x001F)  # blue

            # An opaque pixel equal to the key is nudged so it stays visible;
            # without alpha and without an explicit key nothing is keyed.
            png.write_png(Path(tmp, "logo.png"), 1, 1, [(255, 0, 255, 255)])
            layout = schema.parse_layout(_document(_image(width=1, height=1, transparent="#FF00FF")))
            bitmap = images.load_bitmap(layout_path, layout.images()[0])
            self.assertNotEqual(bitmap.pixels[0], key)
            layout = schema.parse_layout(_document(_image(width=1, height=1)))
            bitmap = images.load_bitmap(layout_path, layout.images()[0])
            self.assertIsNone(bitmap.transparent)
            self.assertEqual(bitmap.pixels[0], key)

            layout = schema.parse_layout(_document(_image(source="missing.png")))
            with self.assertRaises(schema.LayoutError):
                images.load_bitmap(layout_path, layout.images()[0])


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

    def test_ellipse_button_matches_device_hit_test(self):
        """Corners of a round button stay transparent, like InsideEllipse()."""
        layout = schema.parse_layout(_document(
            _button(id="r", x=0, y=0, width=16, height=16, shape="ellipse", label="",
                    style={"fill": "#FF0000", "border_width": 0})))
        canvas = render.Canvas(16, 16, 0x0001)
        render.paint_button(canvas, layout.buttons()[0])
        self.assertEqual(canvas.pixels[0], 0x0001)
        self.assertEqual(canvas.pixels[15], 0x0001)
        self.assertEqual(canvas.pixels[8 * 16 + 8], 0xF800)
        self.assertEqual(canvas.pixels[8 * 16 + 0], 0xF800)
        self.assertTrue(render.inside_ellipse(0, 0, 16, 16, 8, 8))
        self.assertFalse(render.inside_ellipse(0, 0, 16, 16, 0, 0))

    def test_inside_shape_matches_device_predicate(self):
        """Same points as UiCanvas.InsideShapeFollowsEachOutline in ui_test.cpp."""
        inside = lambda shape, px, py: render.inside_shape(shape, 0, 0, 16, 16, px, py)
        self.assertFalse(inside("rounded", 0, 0))
        self.assertTrue(inside("rounded", 1, 1))
        self.assertTrue(inside("rounded", 0, 4))
        self.assertFalse(inside("pill", 1, 1))
        self.assertTrue(inside("pill", 8, 0))
        self.assertTrue(inside("triangle_up", 7, 0))
        self.assertTrue(inside("triangle_up", 8, 0))
        self.assertFalse(inside("triangle_up", 5, 0))
        self.assertTrue(inside("triangle_up", 0, 15))
        self.assertFalse(inside("triangle_down", 0, 15))
        self.assertTrue(inside("triangle_left", 0, 8))
        self.assertFalse(inside("triangle_left", 0, 0))
        self.assertTrue(inside("triangle_right", 15, 8))
        self.assertFalse(inside("triangle_right", 15, 0))
        self.assertTrue(inside("diamond", 8, 0))
        self.assertFalse(inside("diamond", 0, 0))
        canvas = render.Canvas(32, 16)
        canvas.fill_shape("triangle_right", 0, 0, 16, 16, 0x1111)
        canvas.draw_shape_frame("diamond", 16, 0, 16, 16, 2, 0x2222)
        self.assertEqual(canvas.pixels[8 * 32 + 15], 0x1111)
        self.assertEqual(canvas.pixels[15], 0)
        self.assertEqual(canvas.pixels[24], 0x2222)
        self.assertEqual(canvas.pixels[8 * 32 + 24], 0)

    def test_pad_geometry_and_paint_match_device(self):
        """Same case as UiPad in ui_test.cpp: 32x32 pad, RIGHT held."""
        layout = schema.parse_layout(_document(
            _pad(id="p", x=0, y=0, width=32, height=32,
                 style={"fill": "#101010", "pressed_fill": "#202020", "center_fill": "#303030",
                        "border": "#404040", "arrow": "#505050", "border_width": 1})))
        pad = layout.pads()[0]
        self.assertEqual(render.pad_center(pad), (10, 10, 12))
        self.assertEqual(render.pad_segment(pad, 16, 2), 0)
        self.assertEqual(render.pad_segment(pad, 29, 16), 1)
        self.assertEqual(render.pad_segment(pad, 16, 29), 2)
        self.assertEqual(render.pad_segment(pad, 2, 16), 3)
        self.assertEqual(render.pad_segment(pad, 16, 16), render.PAD_CENTER)
        self.assertEqual(render.pad_segment(pad, 0, 0), -1)
        self.assertEqual(render.pad_segment(pad, 22, 9), 1)
        self.assertEqual(render.pad_segment(pad, 22, 22), 2)
        self.assertEqual(render.pad_segment(pad, 9, 22), 3)
        self.assertEqual(render.pad_segment(pad, 9, 9), 0)
        canvas = render.Canvas(32, 32)
        render.paint_pad(canvas, pad, pressed=1)
        self.assertEqual(canvas.pixels[16 * 32 + 16], schema.rgb565("#303030"))
        self.assertEqual(canvas.pixels[16 * 32 + 29], schema.rgb565("#202020"))
        self.assertEqual(canvas.pixels[16 * 32 + 2], schema.rgb565("#101010"))
        self.assertEqual(canvas.pixels[16 * 32 + 27], schema.rgb565("#505050"))
        self.assertEqual(canvas.pixels[16], schema.rgb565("#404040"))
        self.assertEqual(canvas.pixels[5 * 32 + 16], schema.rgb565("#505050"))
        self.assertEqual(canvas.pixels[0], 0)

    def test_dial_geometry_matches_device(self):
        """Same constants as DialPanel: caption 7*scale+4, 270 degree sweep."""
        layout = schema.parse_layout(_document(
            _dial(id="d", x=0, y=0, width=64, height=80, min=0, max=100, step=10, value=0,
                  style={"text_scale": 2, "show_value": False, "face": "#111111",
                         "track": "#222222", "fill": "#333333", "pointer": "#444444"})))
        dial = layout.dials()[0]
        # caption 18 px, free height 62 -> 62 px disc centred horizontally.
        self.assertEqual(render.dial_disc(dial), (1, 18, 62))
        self.assertEqual(render.dial_value_at(dial, 2, 78), 0)     # bottom-left end
        self.assertEqual(render.dial_value_at(dial, 61, 78), 100)  # bottom-right end
        self.assertEqual(render.dial_value_at(dial, 32, 20), 50)   # top centre
        canvas = render.Canvas(64, 96)
        render.paint_dial(canvas, dial, value=50)
        self.assertEqual(canvas.pixels[49 * 64 + 32], schema.rgb565("#111111"))  # face
        self.assertEqual(canvas.pixels[49 * 64 + 3], schema.rgb565("#333333"))   # left: filled
        self.assertEqual(canvas.pixels[49 * 64 + 60], schema.rgb565("#222222"))  # right: track
        self.assertEqual(canvas.pixels[79 * 64 + 32], 0)  # bottom gap stays open

    def test_wheel_and_number_paint(self):
        layout = schema.parse_layout(_document(
            _wheel(id="w", x=0, y=0, width=100, height=66, value=1,
                   style={"text_scale": 2, "fill": "#111111", "highlight": "#222222", "border": "#333333"}),
            _number(id="n", x=0, y=100, width=120, height=40, label="", unit="%", decimals=1,
                    value=1234, style={"text_scale": 1, "fill": None, "text": "#FFFFFF", "align": "right"})))
        wheel, number = layout.wheels()[0], layout.numbers()[0]
        self.assertEqual(render.wheel_row_height(wheel), 22)
        canvas = render.Canvas(200, 200)
        render.paint_wheel(canvas, wheel)
        self.assertEqual(canvas.pixels[2 * 200 + 2], schema.rgb565("#111111"))
        self.assertEqual(canvas.pixels[33 * 200 + 2], schema.rgb565("#222222"))
        self.assertEqual(canvas.pixels[0], schema.rgb565("#333333"))
        self.assertEqual(render.format_number(number, 1234), "123.4%")
        self.assertEqual(render.format_number(number, -5), "-0.5%")
        self.assertEqual(render.format_number(schema.parse_layout(
            _document(_number(decimals=0, unit=""))).numbers()[0], 7), "7")
        render.paint_number(canvas, number, value=1234)
        # Right aligned with padding 4 and vertically centred: the '%' glyph
        # ends at x = 120 - 4 - 1 and its second row has the corner pixel set.
        self.assertEqual(canvas.pixels[117 * 200 + 115], 0xFFFF)
        self.assertEqual(canvas.pixels[117 * 200 + 116], 0)
        self.assertEqual(canvas.pixels[115 * 200 + 115], 0)  # above the text

    def test_image_blit_with_transparency(self):
        bitmap = images.Bitmap(2, 1, [0xF800, 0x07E0], transparent=0x07E0)
        canvas = render.Canvas(4, 1, 0x0001)
        canvas.blit(1, 0, bitmap)
        self.assertEqual(canvas.pixels, [0x0001, 0xF800, 0x0001, 0x0001])
        layout = schema.parse_layout(_document(_image(x=10, y=10, width=2, height=1)))
        with_bitmap = render.render_layout(layout, bitmaps={"logo": bitmap})
        self.assertEqual(with_bitmap.pixels[10 * 800 + 10], 0xF800)
        placeholder = render.render_layout(layout)
        self.assertEqual(placeholder.pixels[10 * 800 + 10], schema.rgb565("#FF00FF"))


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
        layout = schema.load_layout(layout_path)
        bitmaps = emit_cpp.load_bitmaps(layout, layout_path)
        images_path = emit_cpp.images_header_name(header_path)
        expected = emit_cpp.generate_header(layout, layout_path.name, bitmaps, images_path.name)
        self.assertEqual(header_path.read_text(encoding="utf-8"), expected)
        self.assertEqual(images_path.read_text(encoding="utf-8"),
                         emit_cpp.generate_images_header(layout, bitmaps, layout_path.name))

    def test_new_widget_kinds_emission(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout_path = Path(tmp, "ui.json")
            _write_test_png(Path(tmp, "logo.png"))
            layout = schema.parse_layout(_document(
                _button(shape="ellipse"), _dial(on_change="OnPeriod"),
                _wheel(on_change="OnMode"), _number(), _image(width=4, height=2)))
            bitmaps = emit_cpp.load_bitmaps(layout, layout_path)
            with self.assertRaises(ValueError):
                emit_cpp.generate_header(layout, "ui.json")
            text = emit_cpp.generate_header(layout, "ui.json", bitmaps, "ui_layout_images.hpp")
            pixels = emit_cpp.generate_images_header(layout, bitmaps, "ui.json")
        self.assertIn('#include "ui_layout_images.hpp"', text)
        self.assertIn("ui::Icon::kNone,\n        ui::Shape::kEllipse,", text)
        self.assertIn("inline constexpr ui::DialSpec kMainDials[] = {", text)
        self.assertIn("        100, 2000, 100, 500,", text)
        self.assertIn('inline constexpr const char *const kModeItems[] = {\n    "ALL",\n    "PERSON",\n    "FACE",\n};', text)
        self.assertIn("kModeItems, sizeof(kModeItems) / sizeof(kModeItems[0]), 0U,", text)
        self.assertIn("inline constexpr ui::NumberSpec kMainNumbers[] = {", text)
        self.assertIn('        "DET",\n        "%",\n        1U,\n        1234,', text)
        self.assertIn("inline constexpr ui::ImageSpec kMainImages[] = {", text)
        self.assertIn("kLogoPixels,\n        true, 0xF81FU,", text)
        self.assertIn("case WidgetId::kPeriod:\n            handlers.OnPeriod(event);", text)
        self.assertIn("case WidgetId::kMode:\n            handlers.OnMode(event);", text)
        self.assertIn("/* logo.png: 4x2 RGB565 */\n[[gnu::section(\".ui_assets\")]]\n"
                      "inline constexpr std::uint16_t kLogoPixels[] = {", pixels)
        self.assertIn("0xF800U, 0xF800U, 0xF800U, 0xF800U,\n    0xF81FU, 0xF81FU, 0xF81FU, 0x001FU,", pixels)

    def test_images_header_is_omitted_without_images(self):
        layout = schema.parse_layout(_document())
        text = emit_cpp.generate_header(layout, "ui.json", {}, "ui_layout_images.hpp")
        self.assertNotIn("ui_layout_images.hpp", text)

    def test_shape_and_pad_emission(self):
        layout = schema.parse_layout(_document(
            _button(shape="triangle_left"),
            _pad(center=False, on_tap="OnNavTap", on_press="OnNavPress", on_change="OnNavTurn")))
        text = emit_cpp.generate_header(layout, "ui.json")
        self.assertIn("ui::Shape::kTriangleLeft,", text)
        self.assertIn("inline constexpr ui::PadSpec kMainPads[] = {", text)
        self.assertIn("        {500U, 100U, 160U, 160U},\n        false,\n", text)
        self.assertIn("kMainPads, sizeof(kMainPads) / sizeof(kMainPads[0]),", text)
        for event_type, handler in (("kTap", "OnNavTap"), ("kPress", "OnNavPress"),
                                    ("kChange", "OnNavTurn")):
            block = text[text.index(f"ui::EventType::{event_type}"):]
            block = block[:block.index("    }\n")]
            self.assertIn(f"case WidgetId::kNav:\n            handlers.{handler}(event);", block)


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

    def test_new_widget_kinds_via_cli(self):
        from ui_designer.cli import main

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            header = Path(tmp, "out", "ui_layout.hpp")
            images_header = Path(tmp, "out", "ui_layout_images.hpp")
            png_path = Path(tmp, "preview.png")
            Path(tmp, "art").mkdir()
            _write_test_png(Path(tmp, "art", "logo.png"))
            self.assertEqual(main(["init", "--layout", str(layout)]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "round", "--x", "700", "--y", "8",
                "--width", "56", "--height", "56", "--shape", "ellipse", "--icon", "menu"]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "dial", "--id", "period",
                "--x", "500", "--y", "100", "--width", "200", "--height", "220",
                "--min", "100", "--max", "2000", "--step", "100", "--value", "500",
                "--on-change", "OnPeriod"]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "wheel", "--id", "mode",
                "--x", "40", "--y", "200", "--width", "240", "--height", "150",
                "--items", "ALL,PERSON,FACE", "--value", "1", "--on-change", "OnMode"]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "number", "--id", "count",
                "--x", "300", "--y", "200", "--width", "180", "--height", "72",
                "--label", "DET", "--unit", "%", "--decimals", "1", "--fill", "none"]), 0)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "image", "--id", "logo",
                "--x", "40", "--y", "8", "--width", "8", "--height", "4",
                "--source", "art/logo.png", "--header", str(header)]), 0)
            doc = json.loads(layout.read_text())
            widgets = {w["id"]: w for w in doc["screens"][0]["widgets"]}
            self.assertEqual(widgets["round"]["shape"], "ellipse")
            self.assertEqual(widgets["mode"]["items"], ["ALL", "PERSON", "FACE"])
            self.assertEqual(widgets["mode"]["value"], 1)
            self.assertEqual((widgets["count"]["unit"], widgets["count"]["decimals"]), ("%", 1))
            self.assertIsNone(widgets["count"]["style"]["fill"])
            self.assertEqual(widgets["logo"]["source"], "art/logo.png")
            self.assertTrue(images_header.exists())
            self.assertIn("kLogoPixels", header.read_text())
            self.assertEqual(main(["generate", "--layout", str(layout), "--output", str(header), "--check"]), 0)
            images_header.write_text("stale", encoding="utf-8")
            self.assertEqual(main(["generate", "--layout", str(layout), "--output", str(header), "--check"]), 1)

            # Kind-specific options are rejected for other kinds; transparent
            # can be cleared; a missing source fails validation.
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "round", "--items", "A,B"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "period", "--unit", "ms"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "mode", "--value", "3"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "logo", "--transparent", "#00FF00"]), 0)
            self.assertEqual(json.loads(layout.read_text())["screens"][0]["widgets"][4]["transparent"], "#00FF00")
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "logo", "--transparent", "none"]), 0)
            self.assertNotIn("transparent", json.loads(layout.read_text())["screens"][0]["widgets"][4])
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png_path)]), 0)
            self.assertEqual(main(["list", "--layout", str(layout)]), 0)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "logo", "--source", "art/none.png"]), 2)

            # Pads and shaped buttons.
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "pad", "--id", "nav",
                "--x", "560", "--y", "320", "--width", "160", "--height", "160",
                "--center", "false", "--arrow", "#FFE000", "--on-tap", "OnNavTap",
                "--on-change", "OnNavTurn", "--header", str(header)]), 0)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "round", "--shape", "triangle_up"]), 0)
            doc = json.loads(layout.read_text())
            widgets = {w["id"]: w for w in doc["screens"][0]["widgets"]}
            self.assertIs(widgets["nav"]["center"], False)
            self.assertEqual(widgets["nav"]["style"]["arrow"], "#FFE000")
            self.assertEqual(widgets["round"]["shape"], "triangle_up")
            self.assertIn("handlers.OnNavTurn(event);", header.read_text())
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "round", "--center", "true"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "nav", "--label", "X"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png_path)]), 0)

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

    def test_image_endpoint_and_cpp_with_bitmaps(self):
        from ui_designer.server import EditorState, _make_handler

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            _write_test_png(Path(tmp, "logo.png"))
            layout.write_text(json.dumps(_document(_image(width=4, height=2))), encoding="utf-8")
            server = ThreadingHTTPServer(("127.0.0.1", 0), _make_handler(EditorState(layout)))
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                url = f"http://127.0.0.1:{server.server_address[1]}"
                with urllib.request.urlopen(url + "/api/image.png?id=logo") as response:
                    self.assertEqual(response.headers["Content-Type"], "image/png")
                    data = response.read()
                self.assertTrue(data.startswith(b"\x89PNG"))
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    urllib.request.urlopen(url + "/api/image.png?id=nope")
                self.assertEqual(caught.exception.code, 404)
                caught.exception.close()
                with urllib.request.urlopen(url + "/api/cpp") as response:
                    text = response.read().decode()
                self.assertIn("kLogoPixels", text)
                self.assertIn('#include "ui_layout_images.hpp"', text)
                with urllib.request.urlopen(url + "/api/preview.png") as response:
                    self.assertEqual(response.status, 200)
            finally:
                server.shutdown()
                server.server_close()
                worker.join()


if __name__ == "__main__":
    unittest.main()
