import json
import pathlib
import re
import secrets
import subprocess
import sys
import time
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


TOOL_DIR = pathlib.Path(__file__).resolve().parent
APP_ROOT = TOOL_DIR.parents[1]
STATIC_DIR = TOOL_DIR / "static"
CLI_TIMEOUT = 10
GUI_STATE_LIMIT = 10000


def make_handler(declaration=None, *, embedded=False):
    path = declaration if declaration is not None else TOOL_DIR / "example.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    token = secrets.token_urlsafe(32)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *arguments):
            pass

        def send_bytes(self, status, body, content_type):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; "
                             "style-src 'self' https://fonts.googleapis.com; "
                             "font-src 'self' https://fonts.gstatic.com; img-src 'self' data:; "
                             "frame-ancestors " + ("'self'" if embedded else "'none'") + "; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, status, payload):
            self.send_bytes(status, json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8")

        def loopback_host(self):
            port = self.server.server_address[1]
            return self.headers.get("Host") in {f"127.0.0.1:{port}", f"localhost:{port}"}

        def do_GET(self):
            if not self.loopback_host():
                self.send_json(HTTPStatus.FORBIDDEN, {"error": "loopback Host required"})
                return
            route = self.path.split("?", 1)[0]
            if route == "/api/document":
                self.send_json(HTTPStatus.OK, {"document": document, "filename": path.name, "token": token})
                return
            files = {"/": ("index.html", "text/html; charset=utf-8"),
                     "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                     "/style.css": ("style.css", "text/css; charset=utf-8"),
                     "/lucide.min.js": ("lucide.min.js", "text/javascript; charset=utf-8")}
            if route not in files:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            filename, content_type = files[route]
            try:
                self.send_bytes(HTTPStatus.OK, (STATIC_DIR / filename).read_bytes(), content_type)
            except OSError as error:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(error)})

        def do_POST(self):
            if not self.loopback_host() or self.headers.get("X-Editor-Token") != token:
                self.send_json(HTTPStatus.FORBIDDEN, {"error": "invalid editor session"})
                return
            command = self.path.removeprefix("/api/")
            if self.path not in {"/api/check", "/api/simulate", "/api/generate", "/api/evaluate"}:
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 1000000:
                    self.send_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "body must be 1..1000000 bytes"})
                    return
                if self.headers.get_content_type() != "application/json":
                    raise ValueError("application/json required")
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
                if not isinstance(payload, dict) or not isinstance(payload.get("document"), dict):
                    raise ValueError("document must be an object")
                actions = payload.get("actions", [])
                if not isinstance(actions, list) or not all(isinstance(action, str) and re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", action) for action in actions):
                    raise ValueError("actions must be a list of action IDs")
                arguments = [sys.executable, "-m", "tool.feature_constraints", command, "-", "--limit", str(GUI_STATE_LIMIT)]
                if command == "simulate":
                    arguments.extend(["--actions", *actions])
                elif command == "generate":
                    arguments.extend(["--output", "-"])
                request = {"document": payload["document"], "current": payload.get("current"), "change": payload.get("change")} if command == "evaluate" else payload["document"]
                started = time.monotonic()
                process = subprocess.run(arguments, input=json.dumps(request), text=True,
                                         capture_output=True, cwd=APP_ROOT, timeout=CLI_TIMEOUT)
                if process.returncode != 0 and not (command in ("simulate", "evaluate") and process.returncode == 2 and process.stdout):
                    self.send_json(HTTPStatus.UNPROCESSABLE_ENTITY,
                                   {"ok": False, "exit_code": process.returncode, "error": process.stderr.strip()})
                    return
                result = process.stdout if command == "generate" else json.loads(process.stdout)
                self.send_json(HTTPStatus.OK, {"ok": True, "exit_code": process.returncode, "result": result,
                                             "elapsed_ms": round((time.monotonic() - started) * 1000)})
            except subprocess.TimeoutExpired:
                self.send_json(HTTPStatus.GATEWAY_TIMEOUT, {"error": f"CLI exceeded {CLI_TIMEOUT}s"})
            except (ValueError, TypeError, UnicodeError) as error:
                self.send_json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except OSError as error:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(error)})

    return Handler


def create_server(declaration=None, port=0):
    return ThreadingHTTPServer(("127.0.0.1", port), make_handler(declaration))


def serve(declaration, port, open_browser):
    server = create_server(declaration, port)
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"feature-constraints: {url}", flush=True)
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0