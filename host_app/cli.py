"""Shared CLI routing; tools retain their independent command-line interfaces."""

import argparse
from importlib import import_module
import sys


TOOLS = {
    "ui-designer": ("host_app.ui_designer.cli", "UI Designer"),
    "memory-layout": ("host_app.auto_static_memory_layout.cli", "Memory Layout"),
    "ai-model-monitor": ("host_app.ai_model_monitor.ai_model_monitor", "AI Model Monitor"),
    "cpu-task-monitor": ("host_app.cpu_task_monitor.cpu_task_monitor", "CPU Task Monitor"),
    "model-loader": ("userspace.model-loader.tool.cli", "Model Loader"),
    "feature-constraints": ("userspace.experiment-ui-control.tool.feature_constraints.cli", "Feature Constraints"),
}


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(
        prog="python3 -m host_app",
        description="UAI Studio host application. No arguments opens the GUI.",
        epilog="Tool arguments are forwarded unchanged. Use TOOL --help for details.",
    )
    parser.add_argument("tool", choices=["gui", *TOOLS], help="GUI or independent CLI tool")
    if arguments and arguments[0] in ("-h", "--help"):
        parser.print_help()
        return 0
    if not arguments:
        arguments = ["gui"]
    selected = parser.parse_args(arguments[:1]).tool
    module_name = "host_app.gui" if selected == "gui" else TOOLS[selected][0]
    result = import_module(module_name).main(arguments[1:])
    return 0 if result is None else result