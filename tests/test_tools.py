"""
Tests for μAI-Studio dev-tools.

Run: cd uai-studio && python -m pytest tests/ -v
"""

import struct
import sys
import os

_root = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(_root, "tracing", "host"))
sys.path.insert(0, os.path.join(_root, "profiling", "host"))
sys.path.insert(0, os.path.join(_root, "analyzer", "cli"))
import task_monitor
import mcp_client
import probe_compiler
import dashboard


class TestTaskMonitorDecode:
    """Test task_monitor protocol decoding."""

    def _build_report(self, entries):
        """Build a binary task report from entry dicts."""
        buf = struct.pack("<HBH", 0xAB55, 0x10, len(entries))
        for e in entries:
            name_bytes = e["name"].encode("ascii")[:16].ljust(16, b"\x00")
            buf += struct.pack("<HBBI", e["id"], e["state"],
                               e["priority"], e["cpu_us"])
            buf += name_bytes
        return buf

    def test_decode_single_task(self):
        report = self._build_report([
            {"id": 1, "name": "sensor", "state": 1, "priority": 8,
             "cpu_us": 500},
        ])
        entries = task_monitor.decode_task_report(report)
        assert len(entries) == 1
        assert entries[0]["id"] == 1
        assert entries[0]["name"] == "sensor"
        assert entries[0]["state"] == 1
        assert entries[0]["priority"] == 8
        assert entries[0]["cpu_us"] == 500

    def test_decode_multiple_tasks(self):
        report = self._build_report([
            {"id": 1, "name": "task_a", "state": 0, "priority": 10,
             "cpu_us": 100},
            {"id": 2, "name": "task_b", "state": 1, "priority": 5,
             "cpu_us": 2000},
            {"id": 3, "name": "task_c", "state": 2, "priority": 3,
             "cpu_us": 300},
        ])
        entries = task_monitor.decode_task_report(report)
        assert len(entries) == 3
        assert entries[2]["name"] == "task_c"

    def test_decode_bad_magic(self):
        buf = struct.pack("<HBH", 0xFFFF, 0x10, 0)
        entries = task_monitor.decode_task_report(buf)
        assert entries == []

    def test_decode_wrong_type(self):
        buf = struct.pack("<HBH", 0xAB55, 0x01, 0)
        entries = task_monitor.decode_task_report(buf)
        assert entries == []

    def test_decode_too_short(self):
        entries = task_monitor.decode_task_report(b"\x55\xAB")
        assert entries == []

    def test_decode_empty_report(self):
        report = self._build_report([])
        entries = task_monitor.decode_task_report(report)
        assert entries == []



# ================================================================
# MCP Client Tests
# ================================================================

import mcp_client


class TestMcpClientDemo:
    """Test MCP client with demo transport."""

    def test_connect(self):
        client = mcp_client.McpClient.demo()
        info = client.connect()
        assert info is not None
        assert "name" in info
        client.close()

    def test_ping(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        assert client.ping() is True
        client.close()

    def test_list_resources(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        resources = client.list_resources()
        assert len(resources) > 0
        uris = [r["uri"] for r in resources]
        assert "rtos://tasks" in uris
        client.close()

    def test_get_tasks(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        tasks = client.get_tasks()
        assert isinstance(tasks, list)
        assert len(tasks) > 0
        assert "id" in tasks[0]
        assert "name" in tasks[0]
        client.close()

    def test_get_memory(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        mem = client.get_memory()
        assert isinstance(mem, dict)
        assert "total_blocks" in mem
        client.close()

    def test_list_tools(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        tools = client.list_tools()
        assert len(tools) > 0
        names = [t["name"] for t in tools]
        assert "probe_deploy" in names
        client.close()

    def test_deploy_and_call_probe(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        result = client.deploy_probe("test_probe", b'\x00\xbf' * 4)
        assert "deployed" in result.lower() or "slot" in result.lower()

        result = client.call_probe("test_probe")
        assert "executed" in result.lower() or "μs" in result.lower()

        result = client.delete_probe("test_probe")
        assert "deleted" in result.lower()
        client.close()

    def test_get_probes(self):
        client = mcp_client.McpClient.demo()
        client.connect()
        client.deploy_probe("p1", b'\x00' * 8)
        probes = client.get_probes()
        assert isinstance(probes, list)
        assert len(probes) >= 1
        client.close()


# ================================================================
# Probe Compiler Tests
# ================================================================

import probe_compiler


class TestProbeCompiler:
    """Test probe compiler utilities."""

    def test_templates_exist(self):
        assert "timer" in probe_compiler.PROBE_TEMPLATES
        assert "nop" in probe_compiler.PROBE_TEMPLATES

    def test_template_has_source(self):
        for name, info in probe_compiler.PROBE_TEMPLATES.items():
            assert "source" in info
            assert "description" in info
            assert len(info["source"]) > 10

    def test_pic_cflags(self):
        assert "-fPIC" in probe_compiler.PIC_CFLAGS
        assert "-mthumb" in probe_compiler.PIC_CFLAGS
        assert "-nostdlib" in probe_compiler.PIC_CFLAGS

    def test_linker_script(self):
        assert ".text" in probe_compiler.PROBE_LINKER_SCRIPT
        assert "SECTIONS" in probe_compiler.PROBE_LINKER_SCRIPT

    def test_compiler_init(self):
        compiler = probe_compiler.ProbeCompiler()
        assert compiler.cc == "arm-none-eabi-gcc"


# ================================================================
# Dashboard Tests
# ================================================================

import dashboard


class TestDashboard:
    """Test dashboard module."""

    def test_show_banner_no_crash(self):
        # Redirect stdout temporarily
        from io import StringIO
        old_stdout = sys.stdout
        sys.stdout = StringIO()
        try:
            dashboard.show_banner()
            dashboard.show_mode_menu()
        except Exception as e:
            sys.stdout = old_stdout
            assert False, f"Dashboard banner crashed: {e}"
        finally:
            sys.stdout = old_stdout



# (Bytecode compiler and scheduling dashboard tests removed —
#  ported to Rust/WASM in web-analyzer/)
