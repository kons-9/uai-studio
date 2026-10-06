"""CLI for resolving and emitting ai-app memory-layout artifacts."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .common import LayoutError, normalize_layout, read_document, write_if_changed
from .emitters import (
    generate_linker,
    generate_memory_config,
    generate_yaml,
    write_key_header,
    write_raw_header,
)
from .resolver import resolve_document


def _resolve_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--board", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--models-dir", type=Path, required=True)
    parser.add_argument("--model-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)


def _generate_cpp_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--linker-base", type=Path, required=True)
    parser.add_argument("--key-header", type=Path, required=True)
    parser.add_argument("--raw-header", type=Path, required=True)
    parser.add_argument("--memory-config", type=Path, required=True)
    parser.add_argument("--linker", type=Path, required=True)


def _all_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--board", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--models-dir", type=Path, required=True)
    parser.add_argument("--model-config", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--linker-base", type=Path, required=True)


def _repo_root_from(path: Path) -> Path:
    return path.resolve().parents[3]


def _resolve_from_args(args: argparse.Namespace) -> dict:
    return resolve_document(
        _repo_root_from(args.board),
        args.board.resolve(),
        args.application.resolve(),
        args.models_dir.resolve(),
        args.model_config.resolve(),
    )


def _command_resolve(args: argparse.Namespace) -> int:
    document = _resolve_from_args(args)
    write_if_changed(args.output, json.dumps(document, indent=2) + "\n")
    return 0


def _command_generate_yml(args: argparse.Namespace) -> int:
    write_if_changed(args.output, generate_yaml(read_document(args.input.resolve())))
    return 0


def _command_generate_cpp(args: argparse.Namespace) -> int:
    document = read_document(args.input.resolve())
    normalize_layout(document)
    write_key_header(args.key_header, document)
    write_raw_header(args.raw_header, document)
    write_if_changed(args.memory_config, generate_memory_config(document))
    write_if_changed(
        args.linker,
        generate_linker(document, args.linker_base.resolve()),
    )
    return 0


def _command_all(args: argparse.Namespace) -> int:
    output_dir = args.output_dir.resolve()
    document = _resolve_from_args(args)
    json_path = output_dir / "memory_layout.json"
    yaml_path = output_dir / "memory_layout.yml"
    raw_path = (
        output_dir
        / "middleware"
        / "memory"
        / "generated"
        / "static_memory_layout"
        / "raw.hpp"
    )
    key_path = (
        output_dir
        / "middleware"
        / "memory"
        / "generated"
        / "static_memory_layout"
        / "key.hpp"
    )
    config_path = output_dir / "middleware" / "memory" / "generated" / "memory_config.hpp"
    linker_path = output_dir / "stm32n6570-dk-npu-ram.ld"

    write_if_changed(json_path, json.dumps(document, indent=2) + "\n")
    write_if_changed(yaml_path, generate_yaml(document))
    write_key_header(key_path, document)
    write_raw_header(raw_path, document)
    write_if_changed(config_path, generate_memory_config(document))
    write_if_changed(
        linker_path,
        generate_linker(document, args.linker_base.resolve()),
    )
    return 0


def _command_gui(args: argparse.Namespace) -> int:
    # Imported here so the build-time generator does not load the HTTP server.
    from .gui import serve

    return serve(args)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Resolve ai-app memory requirements and emit build artifacts. "
            "Outputs are written only to explicit paths."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve = subparsers.add_parser("resolve", help="collect inputs into resolved JSON")
    _resolve_args(resolve)
    resolve.set_defaults(handler=_command_resolve)

    generate_yml = subparsers.add_parser(
        "generate_yml", help="serialize resolved JSON as YAML"
    )
    generate_yml.add_argument("--input", type=Path, required=True)
    generate_yml.add_argument("--output", type=Path, required=True)
    generate_yml.set_defaults(handler=_command_generate_yml)

    generate_cpp = subparsers.add_parser(
        "generate_cpp",
        help="emit key.hpp, raw.hpp, memory_config.hpp, and linker script",
    )
    _generate_cpp_args(generate_cpp)
    generate_cpp.set_defaults(handler=_command_generate_cpp)

    all_command = subparsers.add_parser(
        "all", help="resolve and emit all outputs below one build directory"
    )
    _all_args(all_command)
    all_command.set_defaults(handler=_command_all)

    default_app = Path(__file__).resolve().parents[2] / "userspace" / "ai-app"
    gui = subparsers.add_parser("gui", help="open the local browser memory-layout editor")
    gui.add_argument("--board", type=Path, default=default_app / "config/board_memory.json")
    gui.add_argument("--application", type=Path, default=default_app / "config/application_memory.json")
    gui.add_argument("--models-dir", type=Path, default=default_app / "models")
    gui.add_argument("--model-config", type=Path, default=default_app / "config/model_layout.json")
    gui.add_argument("--linker-base", type=Path, default=default_app / "stm32n6570-dk-npu-ram.ld")
    gui.add_argument("--port", type=int, default=8766)
    gui.add_argument("--open-browser", action="store_true")
    gui.set_defaults(handler=_command_gui)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv or sys.argv[1:])
    try:
        return args.handler(args)
    except (OSError, LayoutError, ValueError) as error:
        print(f"auto_static_memory_layout: error: {error}", file=sys.stderr)
        return 2
