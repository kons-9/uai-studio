"""5x7 glyph table mirrored from kernel/middleware/ui/canvas.cpp.

``load_device_font`` parses the C++ table so a test can prove the two stay in
sync; the preview renderer and the browser editor use ``GLYPHS``.
"""

from __future__ import annotations

import re
from pathlib import Path

GLYPH_WIDTH = 5
GLYPH_HEIGHT = 7
GLYPH_ADVANCE = 6

GLYPHS: dict[str, tuple[int, ...]] = {
    " ": (0, 0, 0, 0, 0, 0, 0),
    "-": (0, 0, 0, 31, 0, 0, 0),
    "+": (0, 4, 4, 31, 4, 4, 0),
    ".": (0, 0, 0, 0, 0, 12, 12),
    ":": (0, 4, 0, 0, 0, 4, 0),
    "/": (0, 1, 2, 4, 8, 16, 0),
    "%": (24, 25, 2, 4, 8, 19, 3),
    "0": (14, 17, 19, 21, 25, 17, 14),
    "1": (4, 12, 4, 4, 4, 4, 14),
    "2": (14, 17, 1, 2, 4, 8, 31),
    "3": (31, 2, 4, 2, 1, 17, 14),
    "4": (2, 6, 10, 18, 31, 2, 2),
    "5": (31, 16, 30, 1, 1, 17, 14),
    "6": (6, 8, 16, 30, 17, 17, 14),
    "7": (31, 1, 2, 4, 8, 8, 8),
    "8": (14, 17, 17, 14, 17, 17, 14),
    "9": (14, 17, 17, 15, 1, 2, 12),
    "A": (14, 17, 17, 31, 17, 17, 17),
    "B": (30, 17, 17, 30, 17, 17, 30),
    "C": (14, 17, 16, 16, 16, 17, 14),
    "D": (30, 17, 17, 17, 17, 17, 30),
    "E": (31, 16, 16, 30, 16, 16, 31),
    "F": (31, 16, 16, 30, 16, 16, 16),
    "G": (14, 17, 16, 23, 17, 17, 15),
    "H": (17, 17, 17, 31, 17, 17, 17),
    "I": (31, 4, 4, 4, 4, 4, 31),
    "J": (7, 2, 2, 2, 2, 18, 12),
    "K": (17, 18, 20, 24, 20, 18, 17),
    "L": (16, 16, 16, 16, 16, 16, 31),
    "M": (17, 27, 21, 21, 17, 17, 17),
    "N": (17, 17, 25, 21, 19, 17, 17),
    "O": (14, 17, 17, 17, 17, 17, 14),
    "P": (30, 17, 17, 30, 16, 16, 16),
    "Q": (14, 17, 17, 17, 21, 18, 13),
    "R": (30, 17, 17, 30, 20, 18, 17),
    "S": (15, 16, 16, 14, 1, 1, 30),
    "T": (31, 4, 4, 4, 4, 4, 4),
    "U": (17, 17, 17, 17, 17, 17, 14),
    "V": (17, 17, 17, 17, 17, 10, 4),
    "W": (17, 17, 17, 21, 21, 21, 10),
    "X": (17, 17, 10, 4, 10, 17, 17),
    "Y": (17, 17, 17, 10, 4, 4, 4),
    "Z": (31, 1, 2, 4, 8, 16, 31),
}

_ENTRY_RE = re.compile(r"\{'(.)',\s*\{([0-9,\s]+)\}\}")


def supported_characters() -> frozenset[str]:
    return frozenset(GLYPHS)


def glyph_rows(letter: str) -> tuple[int, ...]:
    return GLYPHS.get(letter.upper(), (0,) * GLYPH_HEIGHT)


def text_width(text: str, scale: int) -> int:
    if not text:
        return 0
    return (len(text) * GLYPH_ADVANCE - (GLYPH_ADVANCE - GLYPH_WIDTH)) * scale


def load_device_font(canvas_cpp: Path) -> dict[str, tuple[int, ...]]:
    """Parse the kGlyphs table from canvas.cpp."""
    text = canvas_cpp.read_text(encoding="utf-8")
    start = text.index("kGlyphs[] = {")
    end = text.index("};", start)
    table: dict[str, tuple[int, ...]] = {}
    for match in _ENTRY_RE.finditer(text[start:end]):
        rows = tuple(int(v) for v in match.group(2).split(","))
        table[match.group(1)] = rows
    return table


def device_font_path(repo_root: Path) -> Path:
    return repo_root / "kernel" / "middleware" / "ui" / "canvas.cpp"
