"""Loopback GUI shell mounting editors and running the public tool CLIs."""

import argparse
import base64
import binascii
import json
import mimetypes
import os
from pathlib import Path
import secrets
import shlex
import subprocess
import sys
import tempfile
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib import import_module
from urllib.parse import quote, unquote, urlsplit
import webbrowser

from .cli import TOOLS


ROOT = Path(__file__).resolve().parents[1]
STATIC = Path(__file__).with_name("static")
CONSTRAINTS = ROOT / "userspace/experiment-ui-control/tool/feature_constraints"
CLI_TIMEOUT = 120
MAX_BODY = 16 * 1024 * 1024


class Session:
    def __init__(self):
        self.token = secrets.token_urlsafe(32)
        self.temporary = tempfile.TemporaryDirectory(prefix="uai-host-")
        self.directory = Path(self.temporary.name)
        self.slots = threading.BoundedSemaphore(2)

    def run(self, payload: dict) -> dict:
        tool = payload.get("tool")
        if tool not in TOOLS:
            raise ValueError("unknown CLI tool")
        arguments = payload.get("arguments", [])
        if isinstance(arguments, str):
            arguments = shlex.split(arguments)
        if (not isinstance(arguments, list) or len(arguments) > 256 or
                not all(isinstance(argument, str) and len(argument) <= 8192 and
                        "\x00" not in argument for argument in arguments)):
            raise ValueError("arguments must be a list of strings or CLI argument text")
        if any(argument in {"serve", "gui", "--show", "--open-browser"} for argument in arguments):
            raise ValueError("interactive servers/windows must be started from the CLI")
        if not self.slots.acquire(blocking=False):
            raise ValueError("two CLI operations are already running")
        try:
            return self._execute(tool, arguments, payload.get("upload"))
        finally:
            self.slots.release()

    def _execute(self, tool: str, arguments: list[str], upload) -> dict:
        job = secrets.token_hex(12)
        directory = self.directory / job
        output = directory / "output"
        output.mkdir(parents=True)
        input_path = None
        if upload is not None:
            if not isinstance(upload, dict) or not isinstance(upload.get("name"), str):
                raise ValueError("upload needs a filename and base64 content")
            suffix = Path(upload["name"]).suffix.lower()
            if suffix not in {".bin", ".json", ".log", ".txt", ".yml", ".yaml"}:
                raise ValueError("unsupported input file type")
            input_path = directory / ("input" + suffix)
            try:
                content = base64.b64decode(upload.get("content", ""), validate=True)
            except (ValueError, TypeError, binascii.Error) as error:
                raise ValueError("invalid base64 input") from error
            input_path.write_bytes(content)
        expanded = []
        for argument in arguments:
            if "{input}" in argument:
                if input_path is None:
                    raise ValueError("select an input file before running")
                argument = argument.replace("{input}", str(input_path))
            expanded.append(argument.replace("{output}", str(output)))
        command = [sys.executable, "-m", "host_app", tool, *expanded]
        environment = dict(os.environ, MPLBACKEND="Agg", MPLCONFIGDIR=str(self.directory / "matplotlib"))
        started = time.monotonic()
        process = subprocess.run(
            command, cwd=ROOT, input="", text=True, capture_output=True,
            timeout=CLI_TIMEOUT, env=environment,
        )
        artifacts = []
        for path in sorted(output.rglob("*")):
            if path.is_file() and path.resolve().is_relative_to(output.resolve()):
                relative = path.relative_to(self.directory).as_posix()
                artifacts.append({"name": path.relative_to(output).as_posix(),
                                  "url": "/artifacts/" + quote(relative),
                                  "type": mimetypes.guess_type(path.name)[0] or "application/octet-stream"})
        return {"exit_code": process.returncode, "stdout": process.stdout,
                "stderr": process.stderr, "command": shlex.join(command),
                "elapsed_ms": round((time.monotonic() - started) * 1000),
                "artifacts": artifacts}


def build_parser() -> argparse.ArgumentParser:
    app = ROOT / "userspace/ai-app"
    parser = argparse.ArgumentParser(description="Open the unified UAI Studio host GUI.")
    parser.add_argument("--port", type=int, default=8768, help="localhost port; 0 chooses a free port")
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--layout", type=Path, default=app / "config/ui_layout.json")
    parser.add_argument("--features", type=Path, default=ROOT / "userspace/experiment-ui-control/config/features.json")
    parser.add_argument("--board", type=Path, default=app / "config/board_memory.json")
    parser.add_argument("--application", type=Path, default=app / "config/application_memory.json")
    parser.add_argument("--models-dir", type=Path, default=app / "models")
    parser.add_argument("--model-config", type=Path, default=app / "config/model_layout.json")
    parser.add_argument("--linker-base", type=Path, default=app / "stm32n6570-dk-npu-ram.ld")
    return parser


def create_server(args) -> ThreadingHTTPServer:
    from .ui_designer.server import EditorState, _make_handler
    from .auto_static_memory_layout.gui import Editor, make_handler
    constraint_handler = import_module(TOOLS["feature-constraints"][0].rsplit(".", 1)[0] + ".gui").make_handler

    memory = Editor(ROOT, args.board, args.application, args.models_dir,
                    args.model_config, args.linker_base)
    mounts = {
        "/ui/": _make_handler(EditorState(args.layout), embedded=True),
        "/memory/": make_handler(memory, embedded=True),
        "/constraints/": constraint_handler(args.features, embedded=True),
    }
    session = Session()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *arguments):
            pass

        def send(self, status, body: bytes, content_type: str):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; style-src 'self' https://fonts.googleapis.com; "
                             "font-src 'self' https://fonts.gstatic.com; img-src 'self' blob:; "
                             "frame-src 'self'; frame-ancestors 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def json(self, status, payload):
            self.send(status, json.dumps(payload).encode(), "application/json; charset=utf-8")

        def route(self, method: str) -> bool:
            if self.headers.get("Host") not in {
                f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}",
            }:
                self.json(HTTPStatus.FORBIDDEN, {"error": "loopback Host required"})
                return True
            for prefix, handler_type in mounts.items():
                if self.path.startswith(prefix):
                    mounted = object.__new__(handler_type)
                    mounted.__dict__.update(self.__dict__)
                    mounted.path = self.path[len(prefix) - 1:]
                    getattr(mounted, method)()
                    return True
            return False

        def do_GET(self):
            if self.route("do_GET"):
                return
            path = unquote(urlsplit(self.path).path)
            if path == "/api/session":
                self.json(HTTPStatus.OK, {
                    "token": session.token, "root": str(ROOT),
                    "tools": [{"id": key, "name": value[1]} for key, value in TOOLS.items()],
                })
                return
            assets = {"/": STATIC / "index.html", "/app.js": STATIC / "app.js",
                      "/style.css": STATIC / "style.css",
                      "/lucide.min.js": CONSTRAINTS / "static/lucide.min.js"}
            file = assets.get(path)
            if path.startswith("/artifacts/"):
                file = (session.directory / path.removeprefix("/artifacts/")).resolve()
                if not file.is_relative_to(session.directory.resolve()):
                    self.json(HTTPStatus.FORBIDDEN, {"error": "invalid artifact path"})
                    return
                parts = file.relative_to(session.directory).parts
                if len(parts) < 3 or parts[1] != "output":
                    self.json(HTTPStatus.FORBIDDEN, {"error": "invalid artifact path"})
                    return
            if file is None or not file.is_file():
                self.json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            content_type = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
            self.send(HTTPStatus.OK, file.read_bytes(), content_type)

        def do_POST(self):
            if self.route("do_POST"):
                return
            if self.path != "/api/run":
                self.json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            if self.headers.get("X-Editor-Token") != session.token:
                self.json(HTTPStatus.FORBIDDEN, {"error": "invalid session"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= MAX_BODY:
                    self.json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "request must be 1..16 MiB"})
                    return
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError("request must be an object")
                self.json(HTTPStatus.OK, session.run(payload))
            except subprocess.TimeoutExpired:
                self.json(HTTPStatus.GATEWAY_TIMEOUT, {"error": f"CLI exceeded {CLI_TIMEOUT}s"})
            except (ValueError, TypeError) as error:
                self.json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except OSError as error:
                self.json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(error)})

    try:
        server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    except OSError:
        session.temporary.cleanup()
        raise
    server.session = session
    return server


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        server = create_server(args)
    except (OSError, ValueError) as error:
        print(f"host_app: {error}", file=sys.stderr)
        return 2
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"UAI Studio: {url} (Ctrl-C to stop)", flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        server.session.temporary.cleanup()
    return 0