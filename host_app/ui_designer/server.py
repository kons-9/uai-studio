"""Local HTTP server hosting the browser-based layout editor.

Binds to localhost only. The only file written is the layout path given on
the command line; the browser never chooses paths.
"""

from __future__ import annotations

import json
import secrets
import threading
import urllib.parse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler
from pathlib import Path

from host_app.web_server import create_loopback_server, loopback_host, send_bytes, send_json, serve_loopback

from . import png
from .emit_cpp import generate_header, images_header_name, load_bitmaps
from .font import GLYPHS, GLYPH_ADVANCE, GLYPH_HEIGHT, GLYPH_WIDTH
from .images import load_bitmap
from .render import encode_png, render_layout
from .schema import LayoutError, dump_layout, load_feature_catalog, load_layout, parse_layout, save_layout

STATIC_DIR = Path(__file__).resolve().parent / "static"


def _header_text(state: "EditorState", layout) -> str:
    """Layout header as `generate` would write it next to a hypothetical
    ui_layout.hpp; bitmaps are resolved against the layout file."""
    bitmaps = load_bitmaps(layout, state.layout_path)
    return generate_header(layout, state.layout_path.name, bitmaps,
                           images_header_name(Path("ui_layout.hpp")).name)


class EditorState:
    def __init__(self, layout_path: Path, feature_catalog_path: Path | None = None):
        self.layout_path = layout_path
        self.feature_catalog_path = feature_catalog_path
        self.lock = threading.Lock()
        # Writes require this token so a page on another origin cannot save.
        self.token = secrets.token_urlsafe(32)

    def catalog(self) -> dict[str, frozenset[str]] | None:
        return load_feature_catalog(self.feature_catalog_path) if self.feature_catalog_path else None


def _make_handler(state: EditorState, *, embedded: bool = False):
    ancestors = "'self'" if embedded else "'none'"
    csp = ("default-src 'self'; script-src 'self' 'unsafe-inline'; "
           "style-src 'self' 'unsafe-inline'; img-src 'self'; "
           f"frame-ancestors {ancestors}; base-uri 'none'")

    class Handler(BaseHTTPRequestHandler):
        server_version = "uai-ui-designer/0.1"

        def log_message(self, format: str, *args) -> None:  # noqa: A002
            pass

        def _send(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
            send_bytes(self, status, body, content_type, csp)

        def _json(self, status: HTTPStatus, payload) -> None:
            send_json(self, status, payload, csp)

        def _loopback_host(self) -> bool:
            return loopback_host(self.headers.get("Host"), self.server.server_port)

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
                        features = state.catalog()
                        layout = load_layout(state.layout_path, features)
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                self._json(HTTPStatus.OK, {
                    "path": str(state.layout_path),
                    "token": state.token,
                    "layout": layout.to_document(),
                    "features": {name: sorted(operations) for name, operations in (features or {}).items()},
                })
            elif path == "/api/font":
                self._json(HTTPStatus.OK, {
                    "width": GLYPH_WIDTH, "height": GLYPH_HEIGHT,
                    "advance": GLYPH_ADVANCE, "glyphs": GLYPHS,
                })
            elif path == "/api/cpp":
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path, state.catalog())
                        text = _header_text(state, layout)
                    except LayoutError as error:
                        self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                        return
                self._send(HTTPStatus.OK, text.encode("utf-8"), "text/plain; charset=utf-8")
            elif path == "/api/preview.png":
                with state.lock:
                    try:
                        layout = load_layout(state.layout_path, state.catalog())
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
                        layout = load_layout(state.layout_path, state.catalog())
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
                layout = parse_layout(document, state.catalog())
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


def serve(layout_path: Path, port: int, open_browser: bool,
          feature_catalog_path: Path | None = None) -> int:
    if not layout_path.exists():
        raise LayoutError(f"layout file does not exist: {layout_path}")
    state = EditorState(layout_path, feature_catalog_path)
    load_layout(layout_path, state.catalog())
    server = create_loopback_server(port, _make_handler(state))
    print(f"ui_designer: editing {layout_path}")
    serve_loopback(server, open_browser,
                   lambda url: print(f"ui_designer: open {url} (Ctrl-C to stop)"))
    return 0
