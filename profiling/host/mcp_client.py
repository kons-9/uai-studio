"""
μAI-Studio MCP Client - Model Context Protocol client for profile mode

Connects to the MCU's MCP server via UART or TCP (μAI-Bridge) and
provides a Python API for:
  - Querying RTOS resources (tasks, memory, probes)
  - Deploying/calling/deleting OTA probes
  - Managing trace hooks and filters
  - Interactive AI-driven profiling sessions

Usage (interactive):
    python mcp_client.py --port COM3 --baud 115200
    python mcp_client.py --tcp 192.168.1.100:5005
    python mcp_client.py --demo

Usage (library):
    from mcp_client import McpClient
    client = McpClient.tcp("192.168.1.100", 5005)
    tasks = client.read_resource("rtos://tasks")
"""

import json
import struct
import socket
import argparse
import time
import sys
import os
import base64
from typing import Optional, Dict, Any, List

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
CYAN = "\033[36m"


# ----------------------------------------------------------------
# Transport layer
# ----------------------------------------------------------------

class TcpTransport:
    """TCP connection to MCP server (via μAI-Bridge)."""

    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))

    def send(self, data: bytes):
        if not self.sock:
            raise ConnectionError("Not connected")
        self.sock.sendall(data)

    def recv(self, max_size: int = 4096) -> bytes:
        if not self.sock:
            raise ConnectionError("Not connected")
        return self.sock.recv(max_size)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


class SerialTransport:
    """UART connection to MCP server."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 5.0):
        self.port_name = port
        self.baud = baud
        self.timeout = timeout
        self.serial = None

    def connect(self):
        try:
            import serial
        except ImportError:
            raise ImportError("pyserial required: pip install pyserial")
        self.serial = serial.Serial(
            self.port_name, self.baud, timeout=self.timeout)

    def send(self, data: bytes):
        if not self.serial:
            raise ConnectionError("Not connected")
        self.serial.write(data)

    def recv(self, max_size: int = 4096) -> bytes:
        if not self.serial:
            raise ConnectionError("Not connected")
        return self.serial.read(max_size)

    def close(self):
        if self.serial:
            self.serial.close()
            self.serial = None


class DemoTransport:
    """Simulated transport for demonstration."""

    def __init__(self):
        self.request_queue = []
        self.response_queue = []
        self._setup_demo_data()

    def _setup_demo_data(self):
        self.demo_tasks = [
            {"id": 1, "name": "sensor_read", "state": 1, "priority": 8,
             "stack_free": 512},
            {"id": 2, "name": "ai_bridge", "state": 0, "priority": 6,
             "stack_free": 256},
            {"id": 3, "name": "mcp_server", "state": 1, "priority": 4,
             "stack_free": 384},
        ]
        self.demo_probes = []
        self.probe_counter = 0

    def connect(self):
        pass

    def send(self, data: bytes):
        msg = json.loads(data.decode("utf-8"))
        self.request_queue.append(msg)
        response = self._handle_request(msg)
        self.response_queue.append(json.dumps(response).encode("utf-8"))

    def recv(self, max_size: int = 4096) -> bytes:
        if self.response_queue:
            return self.response_queue.pop(0)
        return b""

    def close(self):
        pass

    def _handle_request(self, msg: dict) -> dict:
        method = msg.get("method", "")
        req_id = msg.get("id", 0)

        if method == "initialize":
            return {
                "jsonrpc": "2.0", "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"resources": {}, "tools": {}},
                    "serverInfo": {"name": "uai-studio-mcu-demo",
                                   "version": "0.1.0"},
                },
            }
        elif method == "ping":
            return {"jsonrpc": "2.0", "id": req_id, "result": {}}
        elif method == "resources/list":
            return {
                "jsonrpc": "2.0", "id": req_id,
                "result": {"resources": [
                    {"uri": "rtos://tasks", "name": "Task list"},
                    {"uri": "rtos://memory", "name": "Memory pools"},
                    {"uri": "rtos://probes", "name": "Probe results"},
                ]},
            }
        elif method == "resources/read":
            uri = msg.get("params", {}).get("uri", "")
            return self._handle_resource_read(req_id, uri)
        elif method == "tools/list":
            return {
                "jsonrpc": "2.0", "id": req_id,
                "result": {"tools": [
                    {"name": "probe_deploy",
                     "description": "Deploy a probe function"},
                    {"name": "probe_call",
                     "description": "Execute a deployed probe"},
                    {"name": "probe_delete",
                     "description": "Remove a deployed probe"},
                    {"name": "trace_attach",
                     "description": "Attach a dynamic trace hook"},
                    {"name": "trace_detach",
                     "description": "Detach a dynamic trace hook"},
                    {"name": "filter_set",
                     "description": "Upload filter bytecode (mini-VM)"},
                    {"name": "filter_clear",
                     "description": "Remove the active trace filter"},
                ]},
            }
        elif method == "tools/call":
            name = msg.get("params", {}).get("name", "")
            args = msg.get("params", {}).get("arguments", {})
            return self._handle_tool_call(req_id, name, args)
        else:
            return {
                "jsonrpc": "2.0", "id": req_id,
                "error": {"code": -32601, "message": "Method not found"},
            }

    def _handle_resource_read(self, req_id, uri):
        if uri == "rtos://tasks":
            text = json.dumps(self.demo_tasks)
        elif uri == "rtos://memory":
            text = json.dumps({"total_blocks": 16, "used_blocks": 7,
                               "block_size": 256})
        elif uri == "rtos://probes":
            text = json.dumps(self.demo_probes)
        else:
            return {
                "jsonrpc": "2.0", "id": req_id,
                "error": {"code": -32602, "message": "Unknown URI"},
            }
        return {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"contents": [
                {"uri": uri, "mimeType": "application/json", "text": text}
            ]},
        }

    def _handle_tool_call(self, req_id, name, args):
        import random
        if name == "probe_deploy":
            self.probe_counter += 1
            probe_name = args.get("name", f"probe_{self.probe_counter}")
            self.demo_probes.append({
                "name": probe_name, "slot": self.probe_counter - 1,
                "call_count": 0, "total_us": 0, "min_us": 0, "max_us": 0,
            })
            text = f"Probe '{probe_name}' deployed to slot " \
                   f"{self.probe_counter - 1}"
        elif name == "probe_call":
            probe_name = args.get("name", "")
            elapsed = random.randint(50, 5000)
            for p in self.demo_probes:
                if p["name"] == probe_name:
                    p["call_count"] += 1
                    p["total_us"] += elapsed
                    if p["min_us"] == 0 or elapsed < p["min_us"]:
                        p["min_us"] = elapsed
                    if elapsed > p["max_us"]:
                        p["max_us"] = elapsed
            text = f"Probe '{probe_name}' executed in {elapsed}μs"
        elif name == "probe_delete":
            probe_name = args.get("name", "")
            self.demo_probes = [
                p for p in self.demo_probes if p["name"] != probe_name]
            text = f"Probe '{probe_name}' deleted"
        elif name == "trace_attach":
            tp = args.get("tracepoint", "UNKNOWN")
            valid_tps = [
                "TASK_SWITCH", "TASK_READY", "TASK_WAIT", "TASK_DORMANT",
                "SEM_SIGNAL", "SEM_WAIT", "MTX_LOCK", "MTX_UNLOCK",
                "MEM_ALLOC", "MEM_FREE", "MEM_CORRUPTION",
                "IRQ_ENTER", "IRQ_EXIT", "USER_EVENT",
            ]
            if tp in valid_tps:
                text = f"Trace hook attached: {tp}"
            else:
                text = f"Unknown tracepoint: {tp}"
                return {
                    "jsonrpc": "2.0", "id": req_id,
                    "result": {
                        "content": [{"type": "text", "text": text}],
                        "isError": True,
                    },
                }
        elif name == "trace_detach":
            tp = args.get("tracepoint", "")
            if tp:
                text = f"Trace hook detached: {tp}"
            else:
                text = "All trace hooks detached"
        elif name == "filter_set":
            b64 = args.get("bytecode_b64", "")
            if b64:
                import base64 as b64mod
                bc = b64mod.b64decode(b64)
                text = f"Filter set ({len(bc)} bytes bytecode)"
            else:
                text = "Missing bytecode_b64"
                return {
                    "jsonrpc": "2.0", "id": req_id,
                    "result": {
                        "content": [{"type": "text", "text": text}],
                        "isError": True,
                    },
                }
        elif name == "filter_clear":
            text = "Filter cleared"
        else:
            return {
                "jsonrpc": "2.0", "id": req_id,
                "result": {
                    "content": [{"type": "text", "text": "Unknown tool"}],
                    "isError": True,
                },
            }
        return {
            "jsonrpc": "2.0", "id": req_id,
            "result": {"content": [{"type": "text", "text": text}]},
        }


# ----------------------------------------------------------------
# MCP Client
# ----------------------------------------------------------------

class McpClient:
    """MCP client for communicating with the MCU's MCP server."""

    def __init__(self, transport):
        self.transport = transport
        self.seq_id = 0
        self.server_info = None

    @classmethod
    def tcp(cls, host: str, port: int):
        t = TcpTransport(host, port)
        return cls(t)

    @classmethod
    def serial(cls, port: str, baud: int = 115200):
        t = SerialTransport(port, baud)
        return cls(t)

    @classmethod
    def demo(cls):
        t = DemoTransport()
        return cls(t)

    def connect(self):
        """Connect and initialize the MCP session."""
        self.transport.connect()
        response = self._call("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "uai-studio-pc",
                "version": "0.1.0",
            },
        })
        self.server_info = response.get("result", {}).get("serverInfo", {})
        return self.server_info

    def close(self):
        self.transport.close()

    def ping(self) -> bool:
        try:
            self._call("ping")
            return True
        except Exception:
            return False

    # ---- Resource operations ----

    def list_resources(self) -> List[Dict]:
        resp = self._call("resources/list")
        return resp.get("result", {}).get("resources", [])

    def read_resource(self, uri: str) -> Any:
        resp = self._call("resources/read", {"uri": uri})
        contents = resp.get("result", {}).get("contents", [])
        if contents:
            text = contents[0].get("text", "")
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                return text
        return None

    def get_tasks(self) -> List[Dict]:
        return self.read_resource("rtos://tasks")

    def get_memory(self) -> Dict:
        return self.read_resource("rtos://memory")

    def get_probes(self) -> List[Dict]:
        return self.read_resource("rtos://probes")

    # ---- Tool operations ----

    def list_tools(self) -> List[Dict]:
        resp = self._call("tools/list")
        return resp.get("result", {}).get("tools", [])

    def call_tool(self, name: str, arguments: Dict = None) -> Dict:
        params = {"name": name}
        if arguments:
            params["arguments"] = arguments
        resp = self._call("tools/call", params)
        result = resp.get("result", {})
        return result

    def deploy_probe(self, name: str, binary: bytes) -> str:
        """Deploy a probe function binary."""
        b64 = base64.b64encode(binary).decode("ascii")
        result = self.call_tool("probe_deploy", {
            "name": name, "binary_b64": b64})
        return self._extract_text(result)

    def call_probe(self, name: str) -> str:
        """Execute a deployed probe."""
        result = self.call_tool("probe_call", {"name": name})
        return self._extract_text(result)

    def delete_probe(self, name: str) -> str:
        """Delete a deployed probe."""
        result = self.call_tool("probe_delete", {"name": name})
        return self._extract_text(result)

    # ---- Trace operations ----

    def trace_attach(self, tracepoint: str) -> str:
        """Attach a dynamic trace hook to a tracepoint."""
        result = self.call_tool("trace_attach",
                                {"tracepoint": tracepoint})
        return self._extract_text(result)

    def trace_detach(self, tracepoint: str = "") -> str:
        """Detach a trace hook. Empty tracepoint = detach all."""
        args = {"tracepoint": tracepoint} if tracepoint else {}
        result = self.call_tool("trace_detach", args)
        return self._extract_text(result)

    def filter_set(self, bytecode_b64: str) -> str:
        """Upload filter bytecode to the mini-VM."""
        result = self.call_tool("filter_set",
                                {"bytecode_b64": bytecode_b64})
        return self._extract_text(result)

    def filter_clear(self) -> str:
        """Remove the active trace filter."""
        result = self.call_tool("filter_clear")
        return self._extract_text(result)

    # ---- Internal ----

    def _call(self, method: str, params: Dict = None) -> Dict:
        self.seq_id += 1
        msg = {
            "jsonrpc": "2.0",
            "id": self.seq_id,
            "method": method,
        }
        if params:
            msg["params"] = params

        data = json.dumps(msg).encode("utf-8")
        self.transport.send(data)

        # Read response
        response_data = self.transport.recv(4096)
        if not response_data:
            raise ConnectionError("No response from MCP server")

        return json.loads(response_data.decode("utf-8"))

    @staticmethod
    def _extract_text(result: Dict) -> str:
        contents = result.get("content", [])
        for c in contents:
            if c.get("type") == "text":
                return c.get("text", "")
        return ""


# ----------------------------------------------------------------
# Interactive CLI
# ----------------------------------------------------------------

def interactive_session(client: McpClient):
    """Interactive MCP profiling session."""
    print(f"\n{BOLD}μAI-Studio MCP Client — Interactive Mode{RESET}")
    print(f"{DIM}Server: {client.server_info}{RESET}")
    print(f"{DIM}Type 'help' for commands, 'quit' to exit.{RESET}\n")

    commands = {
        "help":       "Show available commands",
        "ping":       "Ping the MCP server",
        "tasks":      "Show RTOS task list",
        "memory":     "Show memory pool stats",
        "probes":     "Show deployed probes",
        "resources":  "List available resources",
        "tools":      "List available tools",
        "deploy":     "Deploy a probe (deploy <name>)",
        "call":       "Call a probe (call <name>)",
        "delete":     "Delete a probe (delete <name>)",
        "trace":      "Attach trace hook (trace <tracepoint>)",
        "untrace":    "Detach trace hook (untrace [tracepoint])",
        "filter":     "Set filter bytecode (filter <b64>)",
        "unfilter":   "Clear active filter",
        "quit":       "Exit session",
    }

    while True:
        try:
            line = input(f"{CYAN}mcp>{RESET} ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        parts = line.split(maxsplit=1)
        cmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        try:
            if cmd == "help":
                for k, v in commands.items():
                    print(f"  {GREEN}{k:<12s}{RESET} {v}")

            elif cmd == "ping":
                ok = client.ping()
                print(f"  {'✓ Pong' if ok else '✗ No response'}")

            elif cmd == "tasks":
                tasks = client.get_tasks()
                if isinstance(tasks, list):
                    states = {0: "READY", 1: "RUN", 2: "WAIT", 3: "DORM"}
                    print(f"  {BOLD}{'ID':>4s} {'Name':<16s} {'State':<6s} "
                          f"{'Pri':>3s} {'StackFree':>10s}{RESET}")
                    for t in tasks:
                        st = states.get(t.get("state", 0), "?")
                        print(f"  {t['id']:4d} {t.get('name',''):16s} "
                              f"{st:<6s} {t.get('priority',0):3d} "
                              f"{t.get('stack_free',0):10d}")
                else:
                    print(f"  {tasks}")

            elif cmd == "memory":
                mem = client.get_memory()
                if isinstance(mem, dict):
                    for k, v in mem.items():
                        print(f"  {k}: {v}")
                else:
                    print(f"  {mem}")

            elif cmd == "probes":
                probes = client.get_probes()
                if isinstance(probes, list) and probes:
                    print(f"  {BOLD}{'Name':<16s} {'Calls':>6s} "
                          f"{'Total(μs)':>10s} {'Min':>8s} {'Max':>8s}{RESET}")
                    for p in probes:
                        print(f"  {p.get('name',''):16s} "
                              f"{p.get('call_count',0):6d} "
                              f"{p.get('total_us',0):10d} "
                              f"{p.get('min_us',0):8d} "
                              f"{p.get('max_us',0):8d}")
                else:
                    print(f"  {DIM}(no probes deployed){RESET}")

            elif cmd == "resources":
                res = client.list_resources()
                for r in res:
                    print(f"  {CYAN}{r.get('uri','')}{RESET} — "
                          f"{r.get('name','')}")

            elif cmd == "tools":
                tools = client.list_tools()
                for t in tools:
                    print(f"  {GREEN}{t.get('name','')}{RESET} — "
                          f"{t.get('description','')}")

            elif cmd == "deploy":
                if not arg:
                    print(f"  Usage: deploy <name>")
                    continue
                # Demo: deploy a dummy probe
                dummy_binary = b'\x00\xbf' * 4  # ARM NOP sled
                result = client.deploy_probe(arg, dummy_binary)
                print(f"  {result}")

            elif cmd == "call":
                if not arg:
                    print(f"  Usage: call <name>")
                    continue
                result = client.call_probe(arg)
                print(f"  {result}")

            elif cmd == "delete":
                if not arg:
                    print(f"  Usage: delete <name>")
                    continue
                result = client.delete_probe(arg)
                print(f"  {result}")

            elif cmd == "trace":
                if not arg:
                    print(f"  Usage: trace <tracepoint>")
                    print(f"  Tracepoints: TASK_SWITCH, TASK_READY, "
                          f"TASK_WAIT, SEM_SIGNAL, MTX_LOCK, ...")
                    continue
                result = client.trace_attach(arg.upper())
                print(f"  {result}")

            elif cmd == "untrace":
                result = client.trace_detach(arg.upper() if arg else "")
                print(f"  {result}")

            elif cmd == "filter":
                if not arg:
                    print(f"  Usage: filter <base64_bytecode>")
                    continue
                result = client.filter_set(arg)
                print(f"  {result}")

            elif cmd == "unfilter":
                result = client.filter_clear()
                print(f"  {result}")

            elif cmd in ("quit", "exit", "q"):
                break

            else:
                print(f"  {RED}Unknown command: {cmd}{RESET}")
                print(f"  Type 'help' for available commands.")

        except Exception as e:
            print(f"  {RED}Error: {e}{RESET}")


# ----------------------------------------------------------------
# Main
# ----------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="μAI-Studio MCP Client")
    parser.add_argument("--tcp", type=str, default=None,
                        help="TCP endpoint (host:port)")
    parser.add_argument("--port", type=str, default=None,
                        help="Serial port (e.g. COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Serial baud rate")
    parser.add_argument("--demo", action="store_true",
                        help="Run with simulated MCP server")
    args = parser.parse_args()

    if args.demo:
        client = McpClient.demo()
    elif args.tcp:
        parts = args.tcp.split(":")
        host = parts[0]
        port = int(parts[1]) if len(parts) > 1 else 5005
        client = McpClient.tcp(host, port)
    elif args.port:
        client = McpClient.serial(args.port, args.baud)
    else:
        print("Specify --demo, --tcp, or --port. Use --help for details.")
        sys.exit(1)

    try:
        info = client.connect()
        print(f"{GREEN}Connected to MCP server: {info}{RESET}")
        interactive_session(client)
    except Exception as e:
        print(f"{RED}Connection failed: {e}{RESET}")
        sys.exit(1)
    finally:
        client.close()


if __name__ == "__main__":
    main()
