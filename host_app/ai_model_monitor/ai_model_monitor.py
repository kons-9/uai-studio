#!/usr/bin/env python3
"""Command-line frontend for the ai-app AI model monitor.

Examples:
    python ai_model_monitor.py decode ai_model_monitor.bin -o ai_model_monitor.json
    python ai_model_monitor.py analyze ai_model_monitor.json --cpu-hz 600000000
    python ai_model_monitor.py visualize ai_model_monitor.json -o ai_model_monitor.png
    python ai_model_monitor.py all ai_model_monitor.bin \
        --json ai_model_monitor.json --png ai_model_monitor.png

The ``decode`` and ``analyze`` commands only require the Python standard
library.  ``visualize`` and ``all`` require the dependencies in this
directory's ``pyproject.toml`` (normally run through ``uv``).
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Callable


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))


def load_trace(path: Path) -> dict[str, Any]:
    """Load either a decoded JSON trace or a raw ThreadMonitor dump."""
    if path.suffix.lower() == ".json":
        return json.loads(path.read_text(encoding="utf-8"))

    from decode_thread_monitor import decode

    return decode(path)


def write_trace(trace: dict[str, Any], path: Path, pretty: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(
        trace,
        indent=2 if pretty else None,
        separators=None if pretty else (",", ":"),
    )
    path.write_text(text + "\n", encoding="utf-8")


def default_output(input_path: Path, suffix: str) -> Path:
    """Choose an output beside a raw dump, or the conventional default name."""
    if input_path.suffix.lower() in {".bin", ".json"}:
        return input_path.with_suffix(suffix)
    return input_path.with_name(input_path.name + suffix)


def command_decode(args: argparse.Namespace) -> None:
    from decode_thread_monitor import decode

    trace = decode(args.dump)
    if args.output is None:
        print(
            json.dumps(
                trace,
                indent=2 if args.pretty else None,
                separators=None if args.pretty else (",", ":"),
            )
        )
        return

    write_trace(trace, args.output, args.pretty)
    print(f"decoded {args.dump} -> {args.output}")


def command_analyze(args: argparse.Namespace) -> None:
    from analyze_npu_trace import summarize

    summarize(load_trace(args.trace), max(1, args.top), args.cpu_hz)


def command_visualize(args: argparse.Namespace) -> None:
    from visualize_thread_monitor import plot_trace

    plot_trace(
        load_trace(args.trace),
        args.output,
        args.show,
        args.dpi,
        args.cpu_hz,
        args.max_inferences,
    )


def command_all(args: argparse.Namespace) -> None:
    from analyze_npu_trace import summarize
    from visualize_thread_monitor import plot_trace

    trace = load_trace(args.trace)
    json_output = args.json_output or default_output(args.trace, ".json")
    png_output = args.png_output or default_output(args.trace, ".png")

    write_trace(trace, json_output, args.pretty)
    print(f"decoded {args.trace} -> {json_output}")
    summarize(trace, max(1, args.top), args.cpu_hz)
    plot_trace(
        trace,
        png_output,
        args.show,
        args.dpi,
        args.cpu_hz,
        args.max_inferences,
    )


def add_cpu_hz_argument(
    parser: argparse.ArgumentParser, *, default: float | None
) -> None:
    default_text = (
        f"{default:.0f}" if default is not None and default.is_integer()
        else f"{default:g}" if default is not None
        else ""
    )
    parser.add_argument(
        "--cpu-hz",
        type=float,
        default=default,
        help=(
            f"CPU clock used to convert cycle timings (default: {default_text})"
            if default is not None
            else "optional CPU clock used to convert cycle timings"
        ),
    )


def add_visualize_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("trace", type=Path, help="raw .bin dump or decoded .json")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("ai_model_monitor.png"),
        help="PNG output path (default: ai_model_monitor.png)",
    )
    parser.add_argument("--dpi", type=int, default=140)
    add_cpu_hz_argument(parser, default=600000000.0)
    parser.add_argument("--show", action="store_true", help="also open a window")
    parser.add_argument(
        "--max-inferences",
        type=int,
        default=12,
        help="recent ai_runtime inferences to plot; -1 means all (default: 12)",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    decode_parser = subparsers.add_parser(
        "decode", help="decode a raw dump into JSON"
    )
    decode_parser.add_argument("dump", type=Path, help="raw ThreadMonitor dump")
    decode_parser.add_argument(
        "-o", "--output", type=Path, help="JSON output path; stdout when omitted"
    )
    decode_parser.add_argument("--pretty", action="store_true")
    decode_parser.set_defaults(handler=command_decode)

    analyze_parser = subparsers.add_parser(
        "analyze", help="print timing statistics for a dump or JSON trace"
    )
    analyze_parser.add_argument(
        "trace", type=Path, help="raw .bin dump or decoded .json"
    )
    analyze_parser.add_argument(
        "--top", type=int, default=12, help="number of slow epoch groups to print"
    )
    add_cpu_hz_argument(analyze_parser, default=None)
    analyze_parser.set_defaults(handler=command_analyze)

    visualize_parser = subparsers.add_parser(
        "visualize", help="render a dump or JSON trace as a PNG"
    )
    add_visualize_arguments(visualize_parser)
    visualize_parser.set_defaults(handler=command_visualize)

    all_parser = subparsers.add_parser(
        "all", help="decode, analyze, and visualize a dump or JSON trace"
    )
    all_parser.add_argument(
        "trace", type=Path, help="raw .bin dump or decoded .json"
    )
    all_parser.add_argument(
        "--json",
        dest="json_output",
        type=Path,
        help="decoded JSON output (default: beside the input)",
    )
    all_parser.add_argument(
        "--png",
        dest="png_output",
        type=Path,
        help="PNG output (default: beside the input)",
    )
    all_parser.add_argument(
        "--top", type=int, default=12, help="number of slow epoch groups to print"
    )
    add_cpu_hz_argument(all_parser, default=600000000.0)
    all_parser.add_argument("--dpi", type=int, default=140)
    all_parser.add_argument("--show", action="store_true", help="also open a window")
    all_parser.add_argument(
        "--max-inferences",
        type=int,
        default=12,
        help="recent ai_runtime inferences to plot; -1 means all (default: 12)",
    )
    all_parser.add_argument("--pretty", action="store_true")
    all_parser.set_defaults(handler=command_all)

    return parser


def validate_args(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    if getattr(args, "cpu_hz", None) is not None and args.cpu_hz <= 0.0:
        parser.error("--cpu-hz must be positive")
    if hasattr(args, "max_inferences") and (
        args.max_inferences == 0 or args.max_inferences < -1
    ):
        parser.error("--max-inferences must be positive or -1 for all")
    if hasattr(args, "dpi") and args.dpi <= 0:
        parser.error("--dpi must be positive")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(args, parser)
    try:
        handler: Callable[[argparse.Namespace], None] = args.handler
        handler(args)
    except (ImportError, OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
