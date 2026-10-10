"""CLI for the on-screen UI layout designer."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .emit_cpp import generate_header, generate_images_header, images_header_name, load_bitmaps
from .font import GLYPHS, device_font_path, load_device_font
from .render import render_layout, write_png
from .schema import (
    ALIGNMENTS,
    BUTTON_EVENTS,
    CALLBACK_EVENTS,
    CAMERA_BACKGROUND,
    GEOMETRY_KEYS,
    ICONS,
    SCHEMA_VERSION,
    SHAPES,
    SLIDER_EVENTS,
    SLIDER_RANGE_KEYS,
    STYLE_DEFAULTS,
    WIDGET_TYPES,
    LayoutError,
    dump_layout,
    load_feature_catalog,
    load_layout,
    parse_layout,
)

# Every style key across widget kinds, with the kinds that accept it.
STYLE_OPTIONS: dict[str, tuple[str, ...]] = {}
for _kind, _defaults in STYLE_DEFAULTS.items():
    for _key in _defaults:
        STYLE_OPTIONS[_key] = STYLE_OPTIONS.get(_key, ()) + (_kind,)
INT_STYLE_KEYS = frozenset(
    k for defaults in STYLE_DEFAULTS.values() for k, v in defaults.items()
    if isinstance(v, int) and not isinstance(v, bool))
BOOL_STYLE_KEYS = frozenset(
    k for defaults in STYLE_DEFAULTS.values() for k, v in defaults.items()
    if isinstance(v, bool))
# Per widget kind: the key holding its caption text (None: no caption).
TEXT_KEY = {"button": "label", "label": "text", "slider": "label", "dial": "label",
            "wheel": None, "number": "label", "image": None, "pad": None}
EVENTS_FOR = {"button": BUTTON_EVENTS, "label": (), "slider": SLIDER_EVENTS,
              "dial": SLIDER_EVENTS, "wheel": SLIDER_EVENTS, "number": (), "image": (),
              "pad": CALLBACK_EVENTS}
RANGE_FOR = {"slider": SLIDER_RANGE_KEYS, "dial": SLIDER_RANGE_KEYS, "wheel": ("value",),
             "number": ("value",)}


def _write_if_changed(path: Path, text: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return True


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def feature_catalog_path(layout: Path, catalog: Path | None) -> Path | None:
    if catalog is not None:
        return catalog
    if layout.resolve() == _repo_root() / "userspace/ai-app/config/ui_layout.json":
        return _repo_root() / "userspace/ai-app/config/ui_feature_catalog.json"
    return None


def _catalog(args: argparse.Namespace) -> dict[str, frozenset[str]] | None:
    path = feature_catalog_path(args.layout, args.feature_catalog)
    return load_feature_catalog(path) if path is not None else None


def _command_validate(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout, _catalog(args))
    print(f"{args.layout}: {len(layout.screens)} screen(s), {len(layout.widgets)} widget(s), "
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
    layout = load_layout(args.layout, _catalog(args))
    pressed = frozenset(args.pressed or [])
    checked = frozenset(args.checked or [])
    button_ids = {w.id for w in layout.buttons()}
    for option, ids in (("--pressed", pressed), ("--checked", checked)):
        unknown = ids - button_ids
        if unknown:
            raise LayoutError(f"{option} names unknown buttons: {sorted(unknown)}")
    bitmaps = load_bitmaps(layout, args.layout)
    write_png(args.output, render_layout(layout, pressed, checked, screen_id=args.screen,
                                         bitmaps=bitmaps))
    print(f"wrote {args.output}")
    return 0


def _generated_files(layout, layout_path: Path, output: Path) -> dict[Path, str]:
    """Header text keyed by path: the layout header and, with images, the
    bitmap header next to it."""
    bitmaps = load_bitmaps(layout, layout_path)
    images_path = images_header_name(output)
    files = {output: generate_header(layout, layout_path.name, bitmaps, images_path.name)}
    if layout.images():
        files[images_path] = generate_images_header(layout, bitmaps, layout_path.name)
    return files


def _command_generate(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout, _catalog(args))
    files = _generated_files(layout, args.layout, args.output)
    if args.check:
        stale = [path for path, text in files.items()
                 if not path.exists() or path.read_text(encoding="utf-8") != text]
        if stale:
            for path in stale:
                print(f"{path} is out of date; rerun generate", file=sys.stderr)
            return 1
        print(f"{args.output} is up to date")
        return 0
    for path, text in files.items():
        changed = _write_if_changed(path, text)
        print(f"{'wrote' if changed else 'unchanged'} {path}")
    return 0


def _command_serve(args: argparse.Namespace) -> int:
    from .server import serve

    return serve(args.layout, args.port, not args.no_browser,
                 feature_catalog_path(args.layout, args.feature_catalog))


# --- editing commands -------------------------------------------------------

def _read_document(path: Path) -> dict:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise LayoutError(f"could not read {path}: {error}") from error
    if not isinstance(document, dict):
        raise LayoutError("layout root must be an object")
    # Upgrade legacy single-screen documents in place so edits can add screens.
    if "screens" not in document:
        document["screens"] = [{"id": "main", "background": CAMERA_BACKGROUND,
                                "widgets": document.pop("widgets", [])}]
        document["schema_version"] = SCHEMA_VERSION
    return document


def _save_document(args: argparse.Namespace, document: dict) -> int:
    """Validate, write the layout, and regenerate the header when requested."""
    layout = parse_layout(document, _catalog(args))
    if args.header is not None:
        # Resolve bitmaps before writing so a missing PNG leaves both untouched.
        files = _generated_files(layout, args.layout, args.header)
    elif layout.images():
        load_bitmaps(layout, args.layout)  # image sources must resolve
    args.layout.write_text(dump_layout(layout), encoding="utf-8")
    print(f"wrote {args.layout}")
    if args.header is not None:
        for path, text in files.items():
            changed = _write_if_changed(path, text)
            print(f"{'wrote' if changed else 'unchanged'} {path}")
    return 0


def _screens(document: dict) -> list[dict]:
    screens = document.get("screens")
    if not isinstance(screens, list):
        raise LayoutError("screens must be a list")
    return screens


def _find_screen(document: dict, screen_id: str) -> dict:
    for screen in _screens(document):
        if isinstance(screen, dict) and screen.get("id") == screen_id:
            return screen
    raise LayoutError(f"no screen with id {screen_id!r}")


def _find_widget(document: dict, widget_id: str) -> tuple[dict, dict]:
    for screen in _screens(document):
        for widget in screen.get("widgets", []):
            if isinstance(widget, dict) and widget.get("id") == widget_id:
                return screen, widget
    raise LayoutError(f"no widget with id {widget_id!r}")


def _apply_widget_options(widget: dict, args: argparse.Namespace) -> None:
    widget_type = widget.get("type", "button")
    text_key = TEXT_KEY[widget_type]
    for key in ("label", "text"):
        value = getattr(args, key)
        if value is None:
            continue
        if key != text_key:
            hint = f"; use --{text_key}" if text_key else ""
            raise LayoutError(f"--{key} does not apply to a {widget_type}{hint}")
        widget[key] = value
    for key in GEOMETRY_KEYS:
        value = getattr(args, key)
        if value is not None:
            widget[key] = value

    for key, value in (("icon", args.icon), ("shape", args.shape), ("navigate", args.navigate)):
        if value is None:
            continue
        if widget_type != "button":
            raise LayoutError(f"--{key} applies to buttons only")
        if value in ("", "none", "rectangle"):
            widget.pop(key, None)
        else:
            widget[key] = value
    for key in SLIDER_RANGE_KEYS:
        value = getattr(args, key)
        if value is None:
            continue
        if key not in RANGE_FOR.get(widget_type, ()):
            raise LayoutError(f"--{key} does not apply to a {widget_type}")
        widget[key] = value
    if args.items is not None:
        if widget_type != "wheel":
            raise LayoutError("--items applies to wheels only")
        widget["items"] = [item.strip() for item in args.items.split(",")]
    for key in ("unit", "decimals"):
        value = getattr(args, key)
        if value is None:
            continue
        if widget_type != "number":
            raise LayoutError(f"--{key} applies to numbers only")
        widget[key] = value
    for key in ("source", "transparent"):
        value = getattr(args, key)
        if value is None:
            continue
        if widget_type != "image":
            raise LayoutError(f"--{key} applies to images only")
        if key == "transparent" and value == "none":
            widget.pop(key, None)
        else:
            widget[key] = value
    if args.center is not None:
        if widget_type != "pad":
            raise LayoutError("--center applies to pads only")
        widget["center"] = args.center

    style = widget.setdefault("style", {}) if widget_type != "image" else {}
    for key, kinds in STYLE_OPTIONS.items():
        value = getattr(args, "style_" + key)
        if value is None:
            continue
        if widget_type not in kinds:
            raise LayoutError(f"--{key.replace('_', '-')} does not apply to a {widget_type}")
        # "none" makes a label/number background transparent.
        style[key] = None if (key == "fill" and widget_type in ("label", "number") and
                              value == "none") else value
    for event in CALLBACK_EVENTS:
        value = getattr(args, event)
        if value is None:
            continue
        if event not in EVENTS_FOR[widget_type]:
            raise LayoutError(f"--{event.replace('_', '-')} does not apply to a {widget_type}")
        if value == "":
            widget.pop(event, None)
        else:
            widget[event] = value
    for key in ("feature", "operation"):
        value = getattr(args, key)
        if value is not None:
            if value:
                widget[key] = value
            else:
                widget.pop(key, None)


def _command_init(args: argparse.Namespace) -> int:
    if args.layout.exists() and not args.force:
        raise LayoutError(f"{args.layout} already exists (use --force to overwrite)")
    document = {
        "schema_version": SCHEMA_VERSION,
        "screen": {"width": args.width, "height": args.height},
        "namespace": args.namespace,
        "screens": [{"id": "main", "background": CAMERA_BACKGROUND, "widgets": []}],
    }
    args.layout.parent.mkdir(parents=True, exist_ok=True)
    return _save_document(args, document)


def _command_list(args: argparse.Namespace) -> int:
    layout = load_layout(args.layout, _catalog(args))
    if args.json:
        print(json.dumps(layout.to_document(), indent=2))
        return 0
    print(f"screen {layout.width}x{layout.height}  namespace {layout.namespace}")
    for screen in layout.screens:
        print(f"[{screen.id}] background={screen.background}")
        if not screen.widgets:
            print("  (no widgets)")
            continue
        print(f"  {'id':<20} {'type':<6} {'text':<16} {'x':>5} {'y':>5} {'w':>5} {'h':>5}  extra")
        for widget in screen.widgets:
            extra = [f"{event}={getattr(widget, event)}" for event in CALLBACK_EVENTS
                     if getattr(widget, event)]
            extra += [f"{key}={getattr(widget, key)}" for key in ("feature", "operation")
                      if getattr(widget, key)]
            if widget.is_button and widget.icon != "none":
                extra.append(f"icon={widget.icon}")
            if widget.is_button and widget.navigate:
                extra.append(f"navigate={widget.navigate}")
            if widget.is_button and widget.shape != "rectangle":
                extra.append(f"shape={widget.shape}")
            if widget.is_slider or widget.is_dial:
                extra.append(f"range={widget.minimum}..{widget.maximum}/{widget.step}={widget.value}")
            if widget.is_wheel:
                extra.append(f"items={len(widget.items)} value={widget.value}")
            if widget.is_number:
                extra.append(f"unit={widget.unit!r} decimals={widget.decimals} value={widget.value}")
            if widget.is_image:
                extra.append(f"source={widget.source}")
            if widget.is_pad:
                extra.append(f"center={'yes' if widget.center else 'no'}")
            print(f"  {widget.id:<20} {widget.type:<6} {widget.text[:16]:<16} {widget.x:>5} "
                  f"{widget.y:>5} {widget.width:>5} {widget.height:>5}  {', '.join(extra) or '-'}")
    return 0


def _default_widget(widget_type: str, widget_id: str) -> dict:
    caption = widget_id.upper().replace("_", " ")
    widget = {"type": widget_type, "id": widget_id}
    if TEXT_KEY[widget_type]:
        widget[TEXT_KEY[widget_type]] = caption
    if widget_type in ("slider", "dial"):
        widget.update({"min": 0, "max": 100, "step": 1, "value": 0})
    if widget_type == "wheel":
        widget["items"] = [caption]
    return widget


def _command_add(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    for screen in _screens(document):
        if any(isinstance(w, dict) and w.get("id") == args.id for w in screen.get("widgets", [])):
            raise LayoutError(f"widget {args.id!r} already exists; use 'set' to change it")
    screens = _screens(document)
    screen = _find_screen(document, args.screen) if args.screen else screens[0]
    for key in GEOMETRY_KEYS:
        if getattr(args, key) is None:
            raise LayoutError(f"add requires --{key}")
    widget = _default_widget(args.type, args.id)
    _apply_widget_options(widget, args)
    screen.setdefault("widgets", []).append(widget)
    return _save_document(args, document)


def _command_set(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    screen, widget = _find_widget(document, args.id)
    _apply_widget_options(widget, args)
    if args.rename is not None:
        widget["id"] = args.rename
    if args.screen is not None:
        target = _find_screen(document, args.screen)
        if target is not screen:
            screen["widgets"].remove(widget)
            target.setdefault("widgets", []).append(widget)
    return _save_document(args, document)


def _command_remove(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    screen, widget = _find_widget(document, args.id)
    screen["widgets"].remove(widget)
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


def _command_screen_add(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    if any(isinstance(s, dict) and s.get("id") == args.id for s in _screens(document)):
        raise LayoutError(f"screen {args.id!r} already exists")
    _screens(document).append({"id": args.id, "background": args.background, "widgets": []})
    return _save_document(args, document)


def _command_screen_set(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    screen = _find_screen(document, args.id)
    if args.background is not None:
        screen["background"] = args.background
    if args.rename is not None:
        for s in _screens(document):
            for w in s.get("widgets", []):
                if isinstance(w, dict) and w.get("navigate") == args.id:
                    w["navigate"] = args.rename
        screen["id"] = args.rename
    return _save_document(args, document)


def _command_screen_remove(args: argparse.Namespace) -> int:
    document = _read_document(args.layout)
    screen = _find_screen(document, args.id)
    if screen.get("widgets") and not args.force:
        raise LayoutError(f"screen {args.id!r} still has widgets (use --force)")
    _screens(document).remove(screen)
    return _save_document(args, document)


def _add_widget_options(parser: argparse.ArgumentParser) -> None:
    geometry = parser.add_argument_group("geometry")
    geometry.add_argument("--label", help="button or slider caption")
    geometry.add_argument("--text", help="initial label text")
    for key in GEOMETRY_KEYS:
        geometry.add_argument(f"--{key}", type=int)
    button = parser.add_argument_group("button")
    button.add_argument("--icon", choices=ICONS, help="draw an icon instead of the label")
    button.add_argument("--shape", choices=SHAPES)
    button.add_argument("--navigate", metavar="SCREEN_ID",
                        help="screen shown on tap; '' clears")
    ranged = parser.add_argument_group("slider / dial / wheel / number")
    for key in SLIDER_RANGE_KEYS:
        ranged.add_argument(f"--{key}", type=int)
    ranged.add_argument("--items", metavar="A,B,C", help="wheel items, comma separated")
    ranged.add_argument("--unit", help="number suffix")
    ranged.add_argument("--decimals", type=int, help="number decimal places")
    image = parser.add_argument_group("image")
    image.add_argument("--source", metavar="PNG", help="path relative to the layout file")
    image.add_argument("--transparent", metavar="COLOR", help="#RRGGBB key or 'none'")
    pad = parser.add_argument_group("pad")
    pad.add_argument("--center", type=lambda v: v.lower() in ("1", "true", "yes"),
                     metavar="BOOL", help="draw a centre button")
    style = parser.add_argument_group(
        "style", "colors are #RRGGBB; label --fill accepts 'none' for transparent")
    for key, kinds in STYLE_OPTIONS.items():
        # --text is the label's initial text, so the text color is --text-color.
        option = "--text-color" if key == "text" else "--" + key.replace("_", "-")
        applies = "/".join(kinds)
        dest = "style_" + key
        if key == "align":
            style.add_argument(option, dest=dest, choices=ALIGNMENTS, help=applies)
        elif key in BOOL_STYLE_KEYS:
            style.add_argument(option, dest=dest, type=lambda v: v.lower() in ("1", "true", "yes"),
                               metavar="BOOL", help=applies)
        elif key in INT_STYLE_KEYS:
            style.add_argument(option, dest=dest, type=int, help=applies)
        else:
            style.add_argument(option, dest=dest, help=applies)
    callbacks = parser.add_argument_group(
        "callbacks", "handler method names called by Dispatch(); pass '' to clear")
    for event in CALLBACK_EVENTS:
        callbacks.add_argument("--" + event.replace("_", "-"), dest=event, metavar="METHOD")
    binding = parser.add_argument_group("feature bindings")
    binding.add_argument("--feature", metavar="ID", help="feature id; '' clears")
    binding.add_argument("--operation", metavar="ID", help="operation id; '' clears")


def _add_edit_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--feature-catalog", type=Path, help="feature ids and allowed operations")
    parser.add_argument("--header", type=Path,
                        help="also regenerate this C++ header after saving")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ui_designer",
        description="Edit, preview, and generate C++ for the on-screen UI layout.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("validate", help="check a layout file")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--feature-catalog", type=Path)
    p.add_argument("--check-font", action="store_true",
                   help="also verify the glyph table matches canvas.cpp")
    p.set_defaults(func=_command_validate)

    p = sub.add_parser("render", help="render a PNG preview of one screen")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--feature-catalog", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--screen", metavar="SCREEN_ID", help="screen to draw (default: first)")
    p.add_argument("--pressed", action="append", metavar="ID",
                   help="draw this button in its pressed state (repeatable)")
    p.add_argument("--checked", action="append", metavar="ID",
                   help="draw this button in its checked state (repeatable)")
    p.set_defaults(func=_command_render)

    p = sub.add_parser("generate", help="emit the C++ layout header")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--feature-catalog", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--check", action="store_true",
                   help="exit 1 instead of writing when the output is stale")
    p.set_defaults(func=_command_generate)

    p = sub.add_parser("serve", help="open the browser editor")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--feature-catalog", type=Path)
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--no-browser", action="store_true")
    p.set_defaults(func=_command_serve)

    p = sub.add_parser("init", help="create a layout with one empty camera screen")
    _add_edit_common(p)
    p.add_argument("--width", type=int, default=800)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--namespace", default="uai::ai::app_ui")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=_command_init)

    p = sub.add_parser("list", help="print the screens and widgets in a layout")
    p.add_argument("--layout", type=Path, required=True)
    p.add_argument("--feature-catalog", type=Path)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=_command_list)

    p = sub.add_parser("add", help="add a widget (button, label, slider, dial, wheel, number, image)")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--type", choices=WIDGET_TYPES, default="button")
    p.add_argument("--screen", metavar="SCREEN_ID", help="target screen (default: first)")
    _add_widget_options(p)
    p.set_defaults(func=_command_add)

    p = sub.add_parser("set", help="change properties of a widget")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--rename", metavar="NEW_ID")
    p.add_argument("--screen", metavar="SCREEN_ID", help="move the widget to this screen")
    _add_widget_options(p)
    p.set_defaults(func=_command_set)

    p = sub.add_parser("remove", help="remove a widget")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.set_defaults(func=_command_remove)

    p = sub.add_parser("screen", help="change display size or namespace")
    _add_edit_common(p)
    p.add_argument("--width", type=int)
    p.add_argument("--height", type=int)
    p.add_argument("--namespace")
    p.set_defaults(func=_command_screen)

    p = sub.add_parser("screen-add", help="add a screen (page)")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--background", default=CAMERA_BACKGROUND,
                   help=f"'{CAMERA_BACKGROUND}' or #RRGGBB (default: camera)")
    p.set_defaults(func=_command_screen_add)

    p = sub.add_parser("screen-set", help="change a screen's background or id")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--background")
    p.add_argument("--rename", metavar="NEW_ID")
    p.set_defaults(func=_command_screen_set)

    p = sub.add_parser("screen-remove", help="remove a screen")
    _add_edit_common(p)
    p.add_argument("--id", required=True)
    p.add_argument("--force", action="store_true", help="remove even if it has widgets")
    p.set_defaults(func=_command_screen_remove)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except LayoutError as error:
        print(f"ui_designer: {error}", file=sys.stderr)
        return 2
