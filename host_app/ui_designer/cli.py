"""CLI for the on-screen UI layout designer."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .emit_cpp import generate_header
from .font import GLYPHS, device_font_path, load_device_font
from .render import render_layout, write_png
from .schema import (
    ALIGNMENTS,
    BUTTON_STYLE_DEFAULTS,
    CALLBACK_EVENTS,
    GEOMETRY_KEYS,
    LABEL_STYLE_DEFAULTS,
    SCHEMA_VERSION,
    WIDGET_TYPES,
    LayoutError,
    dump_layout,
    load_layout,
    parse_layout,
)

# Every style key across widget kinds, with the kinds that accept it.
STYLE_OPTIONS: dict[str, tuple[str, ...]] = {}
for _key in BUTTON_STYLE_DEFAULTS:
    STYLE_OPTIONS[_key] = ("button",)
for _key in LABEL_STYLE_DEFAULTS:
    STYLE_OPTIONS[_key] = STYLE_OPTIONS.get(_key, ()) + ("label",)
INT_STYLE_KEYS = frozenset(
    k for k, v in {**LABEL_STYLE_DEFAULTS, **BUTTON_STYLE_DEFAULTS}.items()
    if isinstance(v, int))


def _write_if_changed(path: Path, text: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return True


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _command_validate(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout)
    print(f"{args.layout}: {len(layout.widgets)} widget(s), "
          f"{layout.width}x{layout.height}, namespace {layout.namespace}")
    if args.check_font:
        device = load_device_font(device_font_path(_repo_root()))
        if device != GLYPHS:
            missing = sorted(set(GLYPHS) ^ set(device))
            changed = sorted(k for k in set(GLYPHS) & set(device) if GLYPHS[k] != device[k])
            raise LayoutError(
                f"font table differs from canvas.cpp (missing={missing}, changed={changed})")
        print("font table matches kernel/middleware/ui/canvas.cpp")
    return 0


def _command_render(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout)
    pressed = frozenset(args.pressed or [])
    checked = frozenset(args.checked or [])
    button_ids = {w.id for w in layout.buttons()}
    for option, ids in (("--pressed", pressed), ("--checked", checked)):
        unknown = ids - button_ids
        if unknown:
            raise LayoutError(f"{option} names unknown buttons: {sorted(unknown)}")
    write_png(args.output, render_layout(layout, pressed, checked))
    print(f"wrote {args.output}")
    return 0


def _command_generate(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout)
    text = generate_header(layout, args.layout.name)
    if args.check:
        current = args.output.read_text(encoding="utf-8") if args.output.exists() else None
        if current != text:
            print(f"{args.output} is out of date; rerun generate", file=sys.stderr)
            return 1
        print(f"{args.output} is up to date")
        return 0
    changed = _write_if_changed(args.output, text)
    print(f"{'wrote' if changed else 'unchanged'} {args.output}")
    return 0


def _command_serve(args: argparse.Namespace) -> int:
    from .server import serve

    return serve(args.layout, args.port, not args.no_browser)


# --- editing commands -------------------------------------------------------

def _read_document(path: Path) -> dict:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise LayoutError(f"could not read {path}: {error}") from error
    if not isinstance(document, dict):
        raise LayoutError("layout root must be an object")
    return document


def _save_document(args: argparse.Namespace, document: dict) -> int:
    """Validate, write the layout, and regenerate the header when requested."""
    layout = parse_layout(document)
    args.layout.write_text(dump_layout(layout), encoding="utf-8")
    print(f"wrote {args.layout}")
    if args.header is not None:
        changed = _write_if_changed(args.header, generate_header(layout, args.layout.name))
        print(f"{'wrote' if changed else 'unchanged'} {args.header}")
    return 0


def _find_widget(document: dict, widget_id: str) -> dict:
    for widget in document.get("widgets", []):
        if isinstance(widget, dict) and widget.get("id") == widget_id:
            return widget
    raise LayoutError(f"no widget with id {widget_id!r}")


def _apply_widget_options(widget: dict, args: argparse.Namespace) -> None:
    widget_type = widget.get("type", "button")
    text_key = "label" if widget_type == "button" else "text"
    for key in ("label", "text"):
        value = getattr(args, key)
        if value is None:
            continue
        if key != text_key:
            raise LayoutError(f"--{key} does not apply to a {widget_type}; use --{text_key}")
        widget[key] = value
    for key in GEOMETRY_KEYS:
        value = getattr(args, key)
        if value is not None:
            widget[key] = value
    style = widget.setdefault("style", {})
    for key, kinds in STYLE_OPTIONS.items():
        value = getattr(args, "style_" + key)
        if value is None:
            continue
        if widget_type not in kinds:
            raise LayoutError(f"--{key.replace('_', '-')} does not apply to a {widget_type}")
        # "none" makes a label background transparent.
        style[key] = None if (key == "fill" and widget_type == "label" and
                              value == "none") else value
    for event in CALLBACK_EVENTS:
        value = getattr(args, event)
        if value is None:
            continue
        if widget_type != "button":
            raise LayoutError(f"--{event.replace('_', '-')} applies to buttons only")
        if value == "":
            widget.pop(event, None)
        else:
            widget[event] = value


def _command_init(args: argparse.Namespace) -> int:
    if args.layout.exists() and not args.force:
        raise LayoutError(f"{args.layout} already exists (use --force to overwrite)")
    document = {
        "schema_version": SCHEMA_VERSION,
        "screen": {"width": args.width, "height": args.height},
        "namespace": args.namespace,
        "widgets": [],
    }
    args.layout.parent.mkdir(parents=True, exist_ok=True)
    return _save_document(args, document)


def _command_list(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout)
    if args.json:
        print(json.dumps(layout.to_document(), indent=2))
        return 0
    print(f"screen {layout.width}x{layout.height}  namespace {layout.namespace}")
    if not layout.widgets:
        print("(no widgets)")
        return 0
    print(f"{'id':<20} {'type':<6} {'text':<16} {'x':>5} {'y':>5} {'w':>5} {'h':>5}  callbacks")
    for widget in layout.widgets:
        callbacks = ", ".join(
            f"{event}={getattr(widget, event)}" for event in CALLBACK_EVENTS
            if getattr(widget, event)) or "-"
        print(f"{widget.id:<20} {widget.type:<6} {widget.text[:16]:<16} {widget.x:>5} "
              f"{widget.y:>5} {widget.width:>5} {widget.height:>5}  {callbacks}")
    return 0


def _command_add(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    widgets = document.setdefault("widgets", [])
    if any(isinstance(w, dict) and w.get("id") == args.id for w in widgets):
        raise LayoutError(f"widget {args.id!r} already exists; use 'set' to change it")
    text_key = "label" if args.type == "button" else "text"
    widget = {"type": args.type, "id": args.id,
              text_key: args.id.upper().replace("_", " ")}
    for key in GEOMETRY_KEYS:
        if getattr(args, key) is None:
            raise LayoutError(f"add requires --{key}")
    _apply_widget_options(widget, args)
    widgets.append(widget)
    return _save_document(args, document)


def _command_set(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    widget = _find_widget(document, args.id)
    _apply_widget_options(widget, args)
    if args.rename is not None:
        widget["id"] = args.rename
    return _save_document(args, document)


def _command_remove(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    widget = _find_widget(document, args.id)
    document["widgets"].remove(widget)
    return _save_document(args, document)


def _command_screen(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    screen = document.setdefault("screen", {})
    if args.width is not None:
        screen["width"] = args.width
    if args.height is not None:
        screen["height"] = args.height
    if args.namespace is not None:
        document["namespace"] = args.namespace
    return _save_document(args, document)


def _add_widget_options(parser: argparse.ArgumentParser) -> None:
    geometry = parser.add_argument_group("geometry")
    geometry.add_argument("--label", help="button caption")
    geometry.add_argument("--text", help="initial label text")
    for key in GEOMETRY_KEYS:
        geometry.add_argument(f"--{key}", type=int)
    style = parser.add_argument_group(
        "style", "colors are #RRGGBB; label --fill accepts 'none' for transparent")
    for key, kinds in STYLE_OPTIONS.items():
        # --text is the label's initial text, so the text color is --text-color.
        option = "--text-color" if key == "text" else "--" + key.replace("_", "-")
        applies = "/".join(kinds)
        dest = "style_" + key
        if key == "align":
            style.add_argument(option, dest=dest, choices=ALIGNMENTS, help=applies)
        elif key in INT_STYLE_KEYS:
            style.add_argument(option, dest=dest, type=int, help=applies)
        else:
            style.add_argument(option, dest=dest, help=applies)
    callbacks = parser.add_argument_group(
        "callbacks", "handler method names called by Dispatch(); pass '' to clear")
    for event in CALLBACK_EVENTS:
        callbacks.add_argument("--" + event.replace("_", "-"), dest=event, metavar="METHOD")


def _add_edit_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--header", type=Path,
                        help="also regenerate this C++ header after saving")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ui_designer",
        description="Edit, preview, and generate C++ for the on-screen UI layout.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("validate", help="check a layout file")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--check-font", action="store_true",
                   help="also verify the glyph table matches canvas.cpp")
    p.set_defaults(func=_command_validate)

    p = sub.add_parser("render", help="render a PNG preview of the layout")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--pressed", action="append", metavar="ID",
                   help="draw this button in its pressed state (repeatable)")
    p.add_argument("--checked", action="append", metavar="ID",
                   help="draw this button in its checked state (repeatable)")
    p.set_defaults(func=_command_render)

    p = sub.add_parser("generate", help="emit the C++ layout header")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--check", action="store_true",
                   help="exit 1 instead of writing when the output is stale")
    p.set_defaults(func=_command_generate)

    p = sub.add_parser("serve", help="open the browser editor")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--no-browser", action="store_true")
    p.set_defaults(func=_command_serve)

    p = sub.add_parser("init", help="create an empty layout file")
    _add_edit_common(p)
    p.add_argument("--width", type=int, default=800)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--namespace", default="uai::ai::app_ui")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=_command_init)

    p = sub.add_parser("list", help="print the widgets in a layout")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=_command_list)

    p = sub.add_parser("add", help="add a button or label")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--type", choices=WIDGET_TYPES, default="button")
    _add_widget_options(p)
    p.set_defaults(func=_command_add)

    p = sub.add_parser("set", help="change properties of a widget")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--rename", metavar="NEW_ID")
    _add_widget_options(p)
    p.set_defaults(func=_command_set)

    p = sub.add_parser("remove", help="remove a widget")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.set_defaults(func=_command_remove)

    p = sub.add_parser("screen", help="change screen size or namespace")
    _add_edit_common(p)
    p.add_argument("--width", type=int)
    p.add_argument("--height", type=int)
    p.add_argument("--namespace")
    p.set_defaults(func=_command_screen)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except LayoutError as error:
        print(f"ui_designer: {error}", file=sys.stderr)
        return 2
