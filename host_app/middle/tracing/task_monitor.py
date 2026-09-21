"""
μAI-Studio Task Monitor - Real-time RTOS task monitoring

Receives task monitor reports from μAI-Bridge (via TCP)
and displays real-time task status in a terminal UI.

Usage:
    python task_monitor.py [--port 5001]
"""

import struct
import socket
import argparse
import time
import sys
import os

# Protocol constants (must match uai_protocol.h)
PROTO_MAGIC = 0xAB55
MSG_TASK_REPORT = 0x10

TASK_STATES = {0: "READY", 1: "RUN  ", 2: "WAIT ", 3: "DORM "}
STATE_COLORS = {0: "\033[33m", 1: "\033[32m", 2: "\033[36m", 3: "\033[90m"}
RESET = "\033[0m"
BOLD = "\033[1m"


def decode_task_report(buf: bytes):
    """Decode a task monitor report from wire format."""
    if len(buf) < 5:
        return []

    magic = struct.unpack_from("<H", buf, 0)[0]
    if magic != PROTO_MAGIC:
        return []

    msg_type = buf[2]
    if msg_type != MSG_TASK_REPORT:
        return []

    n_tasks = struct.unpack_from("<H", buf, 3)[0]
    entries = []
    pos = 5
    entry_size = 2 + 1 + 1 + 4 + 16  # 24 bytes

    for _ in range(n_tasks):
        if pos + entry_size > len(buf):
            break
        task_id = struct.unpack_from("<H", buf, pos)[0]
        state = buf[pos + 2]
        priority = buf[pos + 3]
        cpu_us = struct.unpack_from("<I", buf, pos + 4)[0]
        name = buf[pos + 8 : pos + 24].decode("ascii", errors="replace").rstrip("\x00")
        entries.append({
            "id": task_id,
            "state": state,
            "priority": priority,
            "cpu_us": cpu_us,
            "name": name,
        })
        pos += entry_size

    return entries


def clear_screen():
    os.system("cls" if os.name == "nt" else "clear")


def render_dashboard(entries, total_cpu_us):
    """Render the task monitor dashboard."""
    clear_screen()
    print(f"{BOLD}╔══════════════════════════════════════════════════════════╗{RESET}")
    print(f"{BOLD}║          μAI-Studio Task Monitor                        ║{RESET}")
    print(f"{BOLD}╠══════════════════════════════════════════════════════════╣{RESET}")
    print(f"{BOLD}║ ID │ Name           │ State │ Pri │ CPU (μs)  │ CPU %  ║{RESET}")
    print(f"{BOLD}╠════╪════════════════╪═══════╪═════╪═══════════╪════════╣{RESET}")

    for e in entries:
        state_str = TASK_STATES.get(e["state"], "?????")
        color = STATE_COLORS.get(e["state"], "")
        cpu_pct = (e["cpu_us"] / total_cpu_us * 100) if total_cpu_us > 0 else 0.0

        # CPU usage bar
        bar_len = 6
        filled = int(cpu_pct / 100 * bar_len)
        bar = "█" * filled + "░" * (bar_len - filled)

        print(f"║ {e['id']:2d} │ {e['name']:<14s} │ {color}{state_str}{RESET} │ {e['priority']:3d} │ {e['cpu_us']:9d} │ {bar} {cpu_pct:4.1f}%║")

    print(f"{BOLD}╚══════════════════════════════════════════════════════════╝{RESET}")

    if entries:
        # Bottleneck analysis
        sorted_entries = sorted(entries, key=lambda e: e["cpu_us"], reverse=True)
        top = sorted_entries[0]
        if top["cpu_us"] > 0:
            print(f"\n{BOLD}Bottleneck:{RESET} Task '{top['name']}' (ID={top['id']}) "
                  f"using {top['cpu_us']}μs ({top['cpu_us']/total_cpu_us*100:.1f}% CPU)")

    print(f"\nPress Ctrl+C to exit.")


def run_monitor(port: int):
    """Listen for task monitor reports and display dashboard."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)

    print(f"μAI-Studio Task Monitor listening on port {port}...")
    print("Waiting for μAI-Bridge to connect...")

    try:
        conn, addr = srv.accept()
        print(f"Connected: {addr}")

        while True:
            buf = conn.recv(4096)
            if not buf:
                print("Connection closed.")
                break

            entries = decode_task_report(buf)
            if entries:
                total_cpu = sum(e["cpu_us"] for e in entries)
                render_dashboard(entries, total_cpu)

    except KeyboardInterrupt:
        print("\nShutting down.")
    finally:
        srv.close()


def run_demo():
    """Run with simulated data for demonstration."""
    import random

    demo_tasks = [
        {"id": 1, "name": "sensor_read", "priority": 8, "base_cpu": 500},
        {"id": 2, "name": "ai_bridge", "priority": 6, "base_cpu": 2000},
        {"id": 3, "name": "gpio_control", "priority": 10, "base_cpu": 100},
        {"id": 4, "name": "task_monitor", "priority": 2, "base_cpu": 200},
    ]

    print("Running in demo mode (simulated data)...")
    time.sleep(1)

    try:
        step = 0
        while True:
            entries = []
            for t in demo_tasks:
                cpu = t["base_cpu"] + random.randint(-100, 300)
                state = random.choices([0, 1, 2], weights=[3, 1, 2])[0]
                entries.append({
                    "id": t["id"],
                    "name": t["name"],
                    "state": state,
                    "priority": t["priority"],
                    "cpu_us": max(0, cpu),
                })

            total_cpu = sum(e["cpu_us"] for e in entries)
            render_dashboard(entries, total_cpu)
            print(f"  [Demo mode - Step {step}]")
            time.sleep(1)
            step += 1

    except KeyboardInterrupt:
        print("\nDemo stopped.")


def main():
    parser = argparse.ArgumentParser(description="μAI-Studio Task Monitor")
    parser.add_argument("--port", type=int, default=5001, help="Listen port for task reports")
    parser.add_argument("--demo", action="store_true", help="Run with simulated data")
    args = parser.parse_args()

    if args.demo:
        run_demo()
    else:
        run_monitor(args.port)


if __name__ == "__main__":
    main()
