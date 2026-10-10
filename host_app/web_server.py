"""Shared loopback policy and lifecycle for local browser tools."""

from __future__ import annotations

import json
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable


def loopback_host(host: str | None, port: int) -> bool:
    return host in {f"127.0.0.1:{port}", f"localhost:{port}"}


def create_loopback_server(port: int, handler: type[BaseHTTPRequestHandler]) -> ThreadingHTTPServer:
    return ThreadingHTTPServer(("127.0.0.1", port), handler)


def send_bytes(handler: BaseHTTPRequestHandler, status: HTTPStatus, body: bytes,
               content_type: str, csp: str) -> None:
    handler.send_response(status)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("X-Content-Type-Options", "nosniff")
    handler.send_header("Content-Security-Policy", csp)
    handler.end_headers()
    handler.wfile.write(body)


def send_json(handler: BaseHTTPRequestHandler, status: HTTPStatus,
              payload: object, csp: str) -> None:
    send_bytes(handler, status, json.dumps(payload).encode("utf-8"),
               "application/json; charset=utf-8", csp)


def serve_loopback(server: ThreadingHTTPServer, open_browser: bool,
                   announce: Callable[[str], None]) -> None:
    url = f"http://127.0.0.1:{server.server_port}/"
    try:
        announce(url)
        if open_browser:
            webbrowser.open(url)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()