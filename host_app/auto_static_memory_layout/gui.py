"""Loopback-only browser editor using the same resolver and emitters as the CLI."""

from __future__ import annotations

import io
import json
import secrets
import tempfile
import webbrowser
import zipfile
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from .common import LayoutError, align_up, normalize_layout, read_document
from .emitters import (
    generate_linker,
    generate_memory_config,
    generate_yaml,
    write_key_header,
    write_raw_header,
)
from .resolver import resolve_document


STATIC_DIR = Path(__file__).with_name("static")
INPUT_NAMES = ("board", "application", "model_config")


class Editor:
    def __init__(self, root: Path, board: Path, application: Path,
                 models_dir: Path, model_config: Path, linker_base: Path):
        self.root = root.resolve()
        self.paths = dict(zip(INPUT_NAMES, (board, application, model_config)))
        self.models_dir = models_dir.resolve()
        self.linker_base = linker_base.resolve()
        self.token = secrets.token_urlsafe(32)

    def inputs(self) -> dict:
        return {name: read_document(path) for name, path in self.paths.items()}

    def resolve(self, inputs: dict) -> dict:
        if not isinstance(inputs, dict) or any(
            not isinstance(inputs.get(name), dict) for name in INPUT_NAMES
        ):
            raise LayoutError("board, application and model_config must be objects")
        with tempfile.TemporaryDirectory(prefix="uai-memory-") as temporary:
            directory = Path(temporary)
            for name in INPUT_NAMES:
                (directory / f"{name}.json").write_text(
                    json.dumps(inputs[name]), encoding="utf-8"
                )
            document = resolve_document(
                self.root, directory / "board.json", directory / "application.json",
                self.models_dir, directory / "model_config.json",
            )
        layout = normalize_layout(document)
        regions = []
        for memory in layout["memories"]:
            cursor = 0
            allocations = []
            for allocation in layout["allocations"]:
                if allocation["memory"] != memory["name"]:
                    continue
                offset = align_up(cursor, allocation["alignment_value"])
                allocations.append({**allocation, "offset": offset})
                cursor = offset + allocation["size_value"]
            regions.append({**memory, "used": cursor, "allocations": allocations})
        return {"document": document, "regions": regions}

    def export(self, inputs: dict) -> bytes:
        document = self.resolve(inputs)["document"]
        files = {
            "memory_layout.json": json.dumps(document, indent=2) + "\n",
            "memory_layout.yml": generate_yaml(document),
            "middleware/memory/generated/memory_config.hpp": generate_memory_config(document),
            "stm32n6570-dk-npu-ram.ld": generate_linker(document, self.linker_base),
        }
        for name, filename in zip(INPUT_NAMES, (
            "board_memory.json", "application_memory.json", "model_layout.json"
        )):
            files[f"config/{filename}"] = json.dumps(inputs[name], indent=2) + "\n"
        with tempfile.TemporaryDirectory(prefix="uai-memory-export-") as temporary:
            for name, writer in (("key.hpp", write_key_header), ("raw.hpp", write_raw_header)):
                path = Path(temporary) / name
                writer(path, document)
                files[f"middleware/memory/generated/static_memory_layout/{name}"] = (
                    path.read_text(encoding="utf-8")
                )
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, text in files.items():
                archive.writestr(name, text)
        return output.getvalue()


def make_handler(editor: Editor):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args) -> None:
            pass

        def send(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def json(self, status: HTTPStatus, payload: dict) -> None:
            self.send(status, json.dumps(payload).encode(), "application/json; charset=utf-8")

        def allowed_host(self) -> bool:
            return self.headers.get("Host") in {
                f"127.0.0.1:{self.server.server_port}",
                f"localhost:{self.server.server_port}",
            }

        def do_GET(self) -> None:
            if not self.allowed_host():
                self.json(HTTPStatus.FORBIDDEN, {"error": "Loopback Host required"})
                return
            path = self.path.split("?", 1)[0]
            if path == "/api/inputs":
                try:
                    self.json(HTTPStatus.OK, {
                        "inputs": editor.inputs(), "token": editor.token,
                        "paths": {name: str(path.resolve()) for name, path in editor.paths.items()},
                        "models_dir": str(editor.models_dir),
                    })
                except (OSError, ValueError) as error:
                    self.json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})
                return
            assets = {
                "/": ("index.html", "text/html; charset=utf-8"),
                "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                "/style.css": ("style.css", "text/css; charset=utf-8"),
                "/icons/refresh-cw.svg": ("icons/refresh-cw.svg", "image/svg+xml"),
                "/icons/check.svg": ("icons/check.svg", "image/svg+xml"),
                "/icons/download.svg": ("icons/download.svg", "image/svg+xml"),
            }
            if path not in assets:
                self.json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
                return
            filename, content_type = assets[path]
            self.send(HTTPStatus.OK, (STATIC_DIR / filename).read_bytes(), content_type)

        def do_POST(self) -> None:
            if not self.allowed_host() or self.headers.get("X-Editor-Token") != editor.token:
                self.json(HTTPStatus.FORBIDDEN, {"error": "Invalid editor session"})
                return
            if self.path not in {"/api/resolve", "/api/export"}:
                self.json(HTTPStatus.NOT_FOUND, {"error": "Not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 1_000_000:
                    raise LayoutError("Request must contain at most 1 MB of JSON")
                inputs = json.loads(self.rfile.read(length))
                if self.path == "/api/resolve":
                    self.json(HTTPStatus.OK, editor.resolve(inputs))
                else:
                    self.send(HTTPStatus.OK, editor.export(inputs), "application/zip")
            except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
                self.json(HTTPStatus.UNPROCESSABLE_ENTITY, {"error": str(error)})

    return Handler


def serve(args) -> int:
    editor = Editor(
        Path(__file__).resolve().parents[2], args.board, args.application,
        args.models_dir, args.model_config, args.linker_base,
    )
    editor.inputs()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(editor))
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"auto_static_memory_layout: {url} (Ctrl-C to stop)", flush=True)
    if args.open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0