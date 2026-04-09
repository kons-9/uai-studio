"""
μAI-Studio Dashboard - Unified entry point for all monitoring tools

Launches the appropriate tool based on the selected mode:
  - trace:     Task monitor + Bottleneck detector + Memory monitor
  - profile:   MCP client (interactive profiling session)
  - task:      Task monitor only
  - schedule:  Scheduling dashboard only
  - memory:    Memory monitor only
  - bottleneck: Bottleneck detector only

Usage:
    python dashboard.py trace --demo
    python dashboard.py profile --demo
    python dashboard.py task --port 5001
"""

import argparse
import sys
import os
import subprocess
import time

# Add sibling package paths for cross-module imports
_studio_root = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(_studio_root, "tracing", "host"))
sys.path.insert(0, os.path.join(_studio_root, "profiling", "host"))

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
CYAN = "\033[36m"
BLUE = "\033[34m"
MAGENTA = "\033[35m"
BG_BLUE = "\033[44m"
WHITE = "\033[37m"


def show_banner():
    """Display the μAI-Studio banner."""
    os.system("cls" if os.name == "nt" else "clear")
    print(f"""
{BOLD}{CYAN}
    ╔═══════════════════════════════════════════════════╗
    ║                                                   ║
    ║   μAI-Studio                                      ║
    ║   μT-Kernel 3.0 AI Development Support Tools      ║
    ║                                                   ║
    ╚═══════════════════════════════════════════════════╝
{RESET}""")


def show_mode_menu():
    """Display available modes."""
    print(f"{BOLD}Available Modes:{RESET}\n")
    modes = [
        ("profile", "Profile Mode",
         "Interactive MCP profiling with OTA probe deployment"),
        ("task", "Task Monitor",
         "RTOS task state visualization (standalone)"),
        ("schedule", "Scheduling Dashboard",
         "AI model inference pipeline timing (standalone)"),
        ("filter", "Filter Compiler",
         "Compile filter expressions to Mini-VM bytecode"),
        ("web", "Web Dashboard (Rust/WASM)",
         "Open web-analyzer/index.html in browser (serverless)"),
    ]
    for name, title, desc in modes:
        print(f"  {GREEN}{name:<12s}{RESET} {BOLD}{title}{RESET}")
        print(f"  {' ' * 12} {DIM}{desc}{RESET}")
    print()


def cmd_profile(args):
    """Launch profile mode (MCP client)."""
    show_banner()
    print(f"{BOLD}Profile Mode{RESET}")
    print(f"{DIM}Connecting to MCU's MCP server...{RESET}\n")

    from mcp_client import McpClient, interactive_session

    if args.demo:
        client = McpClient.demo()
    elif args.tcp:
        parts = args.tcp.split(":")
        host, port = parts[0], int(parts[1]) if len(parts) > 1 else 5005
        client = McpClient.tcp(host, port)
    else:
        print(f"{YELLOW}Use --demo or --tcp to connect.{RESET}")
        return

    try:
        info = client.connect()
        print(f"{GREEN}Connected: {info}{RESET}")
        interactive_session(client)
    except Exception as e:
        print(f"{RED}Connection failed: {e}{RESET}")
    finally:
        client.close()


def cmd_task(args):
    """Launch task monitor."""
    from task_monitor import run_demo, run_monitor
    if args.demo:
        run_demo()
    else:
        run_monitor(args.port)


def cmd_schedule(args):
    """Scheduling dashboard has been ported to Rust/WASM."""
    print(f"{YELLOW}schedule コマンドは web に移行しました。{RESET}")
    print(f"  python dashboard.py web")
    print(f"{DIM}ブラウザの Scheduling タブを使用してください。{RESET}")


def cmd_filter(args):
    """Filter compiler has been ported to Rust/WASM."""
    print(f"{YELLOW}filter コマンドは web に移行しました。{RESET}")
    print(f"  python dashboard.py web")
    print(f"{DIM}ブラウザの Filter タブを使用してください。{RESET}")


def cmd_web(args):
    """Open the Rust/WASM web analyzer in browser."""
    show_banner()
    print(f"{BOLD}Web Dashboard — Rust/WASM Trace Analyzer{RESET}")
    print(f"{DIM}Opening browser...{RESET}\n")

    import webbrowser
    html_path = os.path.join(os.path.dirname(__file__),
                             "web-analyzer", "index.html")
    if not os.path.exists(html_path):
        print(f"{RED}index.html not found. Run wasm-pack build first.{RESET}")
        return
    webbrowser.open(f"file://{os.path.abspath(html_path)}")
    print(f"{GREEN}Opened: {html_path}{RESET}")
    print(f"{DIM}(Ctrl+C to exit){RESET}")


# ----------------------------------------------------------------
# Main
# ----------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="μAI-Studio — μT-Kernel 3.0 AI Development Support Tools",
        formatter_class=argparse.RawDescriptionHelpFormatter)

    sub = parser.add_subparsers(dest="mode")

    # profile
    p_profile = sub.add_parser("profile", help="Profile Mode (MCP)")
    p_profile.add_argument("--demo", action="store_true")
    p_profile.add_argument("--tcp", type=str, default=None)

    # task
    p_task = sub.add_parser("task", help="Task Monitor")
    p_task.add_argument("--demo", action="store_true")
    p_task.add_argument("--port", type=int, default=5001)

    # schedule
    p_sched = sub.add_parser("schedule", help="Scheduling Dashboard")
    p_sched.add_argument("--demo", action="store_true")
    p_sched.add_argument("--metrics-host", default="127.0.0.1")
    p_sched.add_argument("--metrics-port", type=int, default=5002)

    # filter
    p_filter = sub.add_parser("filter", help="Filter Bytecode Compiler")
    p_filter.add_argument("bc_args", nargs="*", default=[])

    # web
    p_web = sub.add_parser("web", help="Web Dashboard (Rust/WASM)")

    args = parser.parse_args()

    if args.mode is None:
        show_banner()
        show_mode_menu()
        print(f"Usage: python dashboard.py <mode> [options]")
        print(f"       python dashboard.py <mode> --demo")
        return

    handlers = {
        "profile": cmd_profile,
        "task": cmd_task,
        "schedule": cmd_schedule,
        "filter": cmd_filter,
        "web": cmd_web,
    }

    handler = handlers.get(args.mode)
    if handler:
        handler(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
