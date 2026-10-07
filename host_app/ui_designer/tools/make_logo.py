"""Draw the ai-app logo PNG from the 5x7 UI font (reproducible asset).

Usage: python3 host_app/ui_designer/tools/make_logo.py userspace/ai-app/config/ui/logo.png
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from ui_designer.font import GLYPH_HEIGHT, glyph_rows, text_width  # noqa: E402
from ui_designer.png import write_png  # noqa: E402


def main(argv: list[str]) -> int:
    output = Path(argv[1]) if len(argv) > 1 else Path("logo.png")
    text, scale, pad = "UAI", 6, 6
    width = text_width(text, scale) + 2 * pad
    height = GLYPH_HEIGHT * scale + 2 * pad
    pixels = [(0, 0, 0, 0)] * (width * height)

    def fill(x: int, y: int, w: int, h: int, rgba: tuple[int, int, int, int]) -> None:
        for py in range(y, y + h):
            for px in range(x, x + w):
                pixels[py * width + px] = rgba

    # Rounded-looking badge: corners stay transparent.
    for py in range(height):
        for px in range(width):
            if (min(px, width - 1 - px) + min(py, height - 1 - py)) >= 4:
                pixels[py * width + px] = (0x20, 0x60, 0xC0, 255)
    pen = pad
    for letter in text:
        rows = glyph_rows(letter)
        for row in range(GLYPH_HEIGHT):
            for column in range(5):
                if rows[row] & (1 << (4 - column)):
                    fill(pen + column * scale, pad + row * scale, scale, scale, (255, 255, 255, 255))
        pen += 6 * scale
    output.parent.mkdir(parents=True, exist_ok=True)
    write_png(output, width, height, pixels)
    print(f"wrote {output} ({width}x{height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
