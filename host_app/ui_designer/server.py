"""Local HTTP server hosting the browser-based layout editor.

Binds to localhost only. The only file written is the layout path given on
the command line; the browser never chooses paths.
"""

from __future__ import annotations

import json
import secrets
import threading
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import png
from .emit_cpp import generate_header, images_header_name, load_bitmaps
from .font import GLYPHS, GLYPH_ADVANCE, GLYPH_HEIGHT, GLYPH_WIDTH
from .images import load_bitmap
from .render import encode_png, render_layout
from .schema import LayoutError, dump_layout, load_layout, parse_layout, save_layout

STATIC_DIR = Path(__file__).resolve().parent / "static"


def _header_text(state: "EditorState", layout) -> str:
    """Layout header as `generate` would write it next to a hypothetical
    ui_layout.hpp; bitmaps are resolved against the layout file."""
    bitmaps = load_bitmaps(layout, state.layout_path)
    return generate_header(layout, state.layout_path.name, bitmaps,
                           images_header_name(Path("ui_layout.hpp")).name)


class EditorState:
    def __init__(self, layout_path: Path):
        self.layout_path = layout_path
        self.lock = threading.Lock()
        # Writes require this token so a page on another origin cannot save.
        self.token = secrets.token_urlsafe(32)


def _make_handler(state: EditorState):
    class Handler(BaseHTTPRequestHandler):
        server_version = "uai-ui-designer/0.1"

        def log_message(self, format: str, *args) -> None:  # noqa: A002
            pass

        def _send(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header(
                "Content-Security-Policy",
                "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                "style-src 'self' 'unsafe-inline'; img-src 'self'; "
                "frame-ancestors 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, status: HTTPStatus, payload) -> None:
            self._send(status, json.dumps(payload).encode("utf-8"),
                       "application/json; charset=utf-8")

        def _loopback_host(self) -> bool:
            port = self.server.server_address[1]
            return self.headers.get("Host") in {f"127.0.0.1:{port}", f"localhost:{port}"}

        def _read_body(self):
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 1_000_000:
                raise LayoutError("request body is missing or too large")
            return json.loads(self.rfile.read(length).decode("utf-8"))

        def do_GET(self) -> None:  # noqa: N802
            if not self._loopback_host():
                self._json(HTTPStatus.FORBIDDEN, {"error": "loopback Host required"})
                return
            path, _, query = self.path.partition("?")
            params = urllib.parse.parse_qs(query)
            if path == "/":
                self._send(HTTPStatus.OK, (STATIC_DIR / "index.html").read_bytes(),
                           "text/html; charset=utf-8")
            elif path == "/api/layout":
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path)
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                self._json(HTTPStatus.OK, {
                    "path": str(state.layout_path),
                    "token": state.token,
                    "layout": layout.to_document(),
                })
            elif path == "/api/font":
                self._json(HTTPStatus.OK, {
                    "width": GLYPH_WIDTH, "height": GLYPH_HEIGHT,
                    "advance": GLYPH_ADVANCE, "glyphs": GLYPHS,
                })
            elif path == "/api/cpp":
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path)
                        text = _header_text(state, layout)
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                self._send(HTTPStatus.OK, text.encode("utf-8"), "text/plain; charset=utf-8")
            elif path == "/api/preview.png":
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path)
                        screen_id = params.get("screen", [None])[0]
                        canvas = render_layout(layout, screen_id=screen_id,
                                               bitmaps=load_bitmaps(layout, state.layout_path))
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                self._send(HTTPStatus.OK, encode_png(canvas), "image/png")
            elif path == "/api/image.png":
                # The device-side bitmap of one image widget from the saved
                # layout, so the editor shows the exact RGB565 result.
                widget_id = params.get("id", [""])[0]
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path)
                        widget = next(w for w in layout.images() if w.id == widget_id)
                        bitmap = load_bitmap(state.layout_path, widget)
                    except StopIteration:
                        self._json(HTTPStatus.NOT_FOUND, {"error": f"no image {widget_id!r}"})
                        return
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                rgba = [(((pixel >> 11) & 31) * 255 // 31,
                     ((pixel >> 5) & 63) * 255 // 63,
                     (pixel & 31) * 255 // 31,
                     0 if pixel == bitmap.transparent else 255)
                    for pixel in bitmap.pixels]
                self._send(HTTPStatus.OK, png.encode_png(bitmap.width, bitmap.height, rgba), "image/png")
            else:
                self._send(HTTPStatus.NOT_FOUND, b"not found", "text/plain")

        def do_POST(self) -> None:  # noqa: N802
            if (not self._loopback_host() or
                    self.headers.get("X-Editor-Token") != state.token):
                self._json(HTTPStatus.FORBIDDEN, {"error": "invalid editor session"})
                return
            path = self.path.split("?", 1)[0]
            try:
                document = self._read_body()
                layout = parse_layout(document)
            except (LayoutError, ValueError) as error:
                self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                return
            if path == "/api/validate":
                self._json(HTTPStatus.OK, {"ok": True, "layout": layout.to_document()})
            elif path == "/api/cpp":
                try:
                    text = _header_text(state, layout)
                except LayoutError as error:
                    self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                    return
                self._send(HTTPStatus.OK, text.encode("utf-8"), "text/plain; charset=utf-8")
            elif path == "/api/layout":
                with state.lock:
                    save_layout(state.layout_path, layout)
                self._json(HTTPStatus.OK, {"ok": True, "text": dump_layout(layout)})
            else:
                self._send(HTTPStatus.NOT_FOUND, b"not found", "text/plain")

    return Handler


def serve(layout_path: Path, port: int, open_browser: bool) -> int:
    if not layout_path.exists():
        raise LayoutError(f"layout file does not exist: {layout_path}")
    load_layout(layout_path)
    server = ThreadingHTTPServer(("127.0.0.1", port), _make_handler(EditorState(layout_path)))
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"ui_designer: editing {layout_path}")
    print(f"ui_designer: open {url} (Ctrl-C to stop)")
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0
