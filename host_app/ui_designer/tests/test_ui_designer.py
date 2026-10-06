"""Unit tests: python3 -m unittest discover -s host_app/ui_designer/tests -t host_app"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from ui_designer import emit_cpp, font, render, schema

REPO_ROOT = Path(__file__).resolve().parents[3]


def _document(**overrides):
    widget = {
        "type": "button", "id": "toggle_boxes", "label": "BOXES",
        "x": 624, "y": 392, "width": 160, "height": 72,
    }
    widget.update(overrides.pop("widget", {}))
    document = {
        "schema_version": 1,
        "screen": {"width": 800, "height": 480},
        "namespace": "uai::ai::app_ui",
        "widgets": [widget],
    }
    document.update(overrides)
    return document


def _label(**overrides):
    label = {"type": "label", "id": "status", "text": "HELLO",
             "x": 0, "y": 0, "width": 800, "height": 24}
    label.update(overrides)
    return label


class SchemaTest(unittest.TestCase):
    def test_defaults_and_round_trip(self):
        layout = schema.parse_layout(_document())
        self.assertEqual(layout.widgets[0].style.text_scale, 3)
        self.assertEqual(layout.widgets[0].style.fill, "#2060C0")
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

    def test_rejects_out_of_screen_widget(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"x": 700, "width": 200}))

    def test_rejects_bad_identifier_and_duplicates(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"id": "ToggleBoxes"}))
        document = _document()
        document["widgets"].append(dict(document["widgets"][0], y=16))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(document)

    def test_rejects_label_without_glyph(self):
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"label": "ボタン"}))
        schema.parse_layout(_document(widget={"label": "boxes 1/2"}))

    def test_rejects_overlap(self):
        document = _document()
        document["widgets"].append(
            dict(document["widgets"][0], id="second", x=700, width=80))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(document)

    def test_callbacks_are_cpp_identifiers_and_round_trip(self):
        layout = schema.parse_layout(_document(widget={"on_tap": "OnBoxes"}))
        self.assertEqual(layout.widgets[0].on_tap, "OnBoxes")
        self.assertEqual(layout.widgets[0].on_press, "")
        document = layout.to_document()
        self.assertEqual(document["widgets"][0]["on_tap"], "OnBoxes")
        self.assertNotIn("on_press", document["widgets"][0])
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"on_tap": "on-boxes"}))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"on_tab": "OnBoxes"}))

    def test_rgb565_matches_device_formula(self):
        self.assertEqual(schema.rgb565("#FFFFFF"), 0xFFFF)
        self.assertEqual(schema.rgb565("#FF0000"), 0xF800)
        self.assertEqual(schema.rgb565("#00FF00"), 0x07E0)
        self.assertEqual(schema.rgb565("#0000FF"), 0x001F)

    def test_label_widget_defaults_and_constraints(self):
        document = _document()
        document["widgets"].append(_label(style={"fill": None, "align": "right"}))
        layout = schema.parse_layout(document)
        label = layout.labels()[0]
        self.assertEqual(label.text, "HELLO")
        self.assertIsNone(label.style.fill)
        self.assertEqual(label.style.align, "right")
        self.assertEqual(label.style.text_scale, 2)
        self.assertEqual(label.style.padding, 4)
        self.assertEqual(len(layout.buttons()), 1)
        again = schema.parse_layout(json.loads(schema.dump_layout(layout)))
        self.assertEqual(again, layout)

        for bad in (
            _label(on_tap="OnX"),
            _label(label="X"),
            _label(style={"align": "middle"}),
            _label(style={"border": "#FFFFFF"}),
            _label(text="A" * schema.LABEL_TEXT_CAPACITY),
        ):
            with self.assertRaises(schema.LayoutError, msg=str(bad)):
                schema.parse_layout(_document(widgets=[bad]))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widget={"text": "X"}))
        with self.assertRaises(schema.LayoutError):
            schema.parse_layout(_document(widgets=[_label(type="slider")]))


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

    def test_button_paint_and_pressed_state(self):
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

    def test_label_paint_alignment_and_transparency(self):
        document = _document(widgets=[
            _label(id="a", text="I", x=0, y=0, width=32, height=9,
                   style={"text_scale": 1, "align": "right", "padding": 4, "fill": None}),
            _label(id="b", text="", x=0, y=20, width=16, height=8, style={"fill": "#FF0000"}),
        ])
        canvas = render.render_layout(schema.parse_layout(document))
        self.assertEqual(canvas.pixels[1 * 800 + 27], 0xFFFF)
        self.assertEqual(canvas.pixels[1 * 800 + 28], 0x4208)
        self.assertEqual(canvas.pixels[0], 0x4208)  # transparent background
        self.assertEqual(canvas.pixels[20 * 800 + 3], 0xF800)

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
        self.assertIn("kToggleBoxes = 1U,", text)
        self.assertIn('"BOXES"', text)
        self.assertIn("{624U, 392U, 160U, 72U}", text)
        self.assertIn("ui::Rgb565(0x20U, 0x60U, 0xC0U)", text)
        self.assertIn("ui::Rgb565(0x00U, 0xA0U, 0x60U)", text)  # checked_fill
        self.assertIn("namespace uai::ai::app_ui {", text)
        self.assertIn("kLabels = nullptr", text)

    def test_label_emission(self):
        document = _document()
        document["widgets"].append(_label(style={"fill": None, "align": "center", "padding": 0}))
        text = emit_cpp.generate_header(schema.parse_layout(document), "x.json")
        self.assertIn("kStatus = 2U,", text)
        self.assertIn("inline constexpr ui::LabelSpec kLabels[] = {", text)
        self.assertIn('"HELLO"', text)
        self.assertIn("            false,", text)
        self.assertIn("ui::TextAlign::kCenter,", text)
        self.assertIn("            0U,\n        },", text)
        self.assertNotIn("kStatus:", text[text.index("Dispatch("):])

    def test_empty_layout_header(self):
        layout = schema.parse_layout(_document(widgets=[]))
        text = emit_cpp.generate_header(layout, "x.json")
        self.assertIn("kButtons = nullptr", text)
        self.assertIn("kButtonCount = 0U", text)
        self.assertIn("bool Dispatch(Handlers &handlers, const ui::Event &event)", text)
        self.assertIn("(void)handlers;", text)

    def test_dispatch_routes_each_bound_event(self):
        document = _document(widget={"on_tap": "OnBoxes", "on_press": "OnBoxesDown"})
        document["widgets"].append({
            "type": "button", "id": "quiet", "label": "Q",
            "x": 16, "y": 16, "width": 80, "height": 40})
        text = emit_cpp.generate_header(schema.parse_layout(document), "x.json")
        self.assertIn("void OnBoxes(const ui::Event &event);", text)
        self.assertIn("void OnBoxesDown(const ui::Event &event);", text)
        tap = text.index("ui::EventType::kTap")
        press = text.index("ui::EventType::kPress")
        self.assertIn("handlers.OnBoxes(event);", text[tap:press])
        self.assertIn("handlers.OnBoxesDown(event);", text[press:])
        self.assertNotIn("kQuiet:", text[text.index("Dispatch("):])

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
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--pressed", "nope"]), 2)

    def test_edit_commands(self):
        from ui_designer.cli import main

        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp, "ui.json")
            header = Path(tmp, "ui_layout.hpp")
            self.assertEqual(main(["init", "--layout", str(layout)]), 0)
            self.assertEqual(main(["init", "--layout", str(layout)]), 2)
            self.assertEqual(main([
                "add", "--layout", str(layout), "--id", "start", "--x", "16", "--y", "392",
                "--width", "160", "--height", "72", "--on-tap", "OnStart",
                "--fill", "#00AA00", "--header", str(header)]), 0)
            doc = json.loads(layout.read_text())
            self.assertEqual(doc["widgets"][0]["label"], "START")
            self.assertEqual(doc["widgets"][0]["on_tap"], "OnStart")
            self.assertEqual(doc["widgets"][0]["style"]["fill"], "#00AA00")
            self.assertEqual(doc["widgets"][0]["style"]["text_scale"], 3)
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
            self.assertEqual(doc["widgets"][0]["id"], "go")
            self.assertNotIn("on_tap", doc["widgets"][0])
            self.assertEqual(doc["widgets"][0]["on_press"], "OnGoDown")
            self.assertIn("kGo = 1U", header.read_text())

            self.assertEqual(main(["screen", "--layout", str(layout), "--namespace", "demo::ui"]), 0)
            self.assertEqual(main(["screen", "--layout", str(layout), "--width", "100"]), 2)
            self.assertEqual(main(["list", "--layout", str(layout)]), 0)

            # Labels: --text, --align, transparent fill; button-only options rejected.
            self.assertEqual(main([
                "add", "--layout", str(layout), "--type", "label", "--id", "fps_label",
                "--x", "0", "--y", "0", "--width", "300", "--height", "24",
                "--align", "center", "--fill", "none", "--text-color", "#FFE000"]), 0)
            doc = json.loads(layout.read_text())
            fps = next(w for w in doc["widgets"] if w["id"] == "fps_label")
            self.assertEqual(fps["text"], "FPS LABEL")
            self.assertIsNone(fps["style"]["fill"])
            self.assertEqual(fps["style"]["text"], "#FFE000")
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--text", "12.5 FPS"]), 0)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--on-tap", "OnX"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "fps_label", "--border-width", "1"]), 2)
            self.assertEqual(main(["set", "--layout", str(layout), "--id", "go", "--text", "X"]), 2)
            png = Path(tmp, "edit.png")
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--checked", "fps_label"]), 2)
            self.assertEqual(main(["render", "--layout", str(layout), "--output", str(png), "--checked", "go"]), 0)
            self.assertEqual(main(["remove", "--layout", str(layout), "--id", "fps_label"]), 0)

            self.assertEqual(main(["remove", "--layout", str(layout), "--id", "go"]), 0)
            self.assertEqual(main(["remove", "--layout", str(layout), "--id", "go"]), 2)
            self.assertEqual(json.loads(layout.read_text())["widgets"], [])


class ServerTest(unittest.TestCase):
    def test_writes_require_loopback_host_and_session_token(self):
        import threading
        import urllib.error
        import urllib.request
        from http.server import ThreadingHTTPServer

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
                body = json.dumps(_document(widget={"x": 16})).encode()
                for headers, status in (
                    ({}, 403),
                    ({"X-Editor-Token": token, "Host": "evil.example"}, 403),
                ):
                    request = urllib.request.Request(url + "/api/layout", data=body, headers=headers)
                    with self.assertRaises(urllib.error.HTTPError) as caught:
                        urllib.request.urlopen(request)
                    self.assertEqual(caught.exception.code, status)
                    caught.exception.close()
                self.assertEqual(json.loads(layout.read_text())["widgets"][0]["x"], 624)

                request = urllib.request.Request(
                    url + "/api/layout", data=body, headers={"X-Editor-Token": token})
                with urllib.request.urlopen(request) as response:
                    self.assertTrue(json.load(response)["ok"])
                self.assertEqual(json.loads(layout.read_text())["widgets"][0]["x"], 16)
            finally:
                server.shutdown()
                server.server_close()
                worker.join()


if __name__ == "__main__":
    unittest.main()
