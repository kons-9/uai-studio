"""Generate the ai-app identity and Japanese settings captions.

Requires Pillow and Japanese fonts; neither is needed to run ui_designer.
Usage: uv run --no-project --with Pillow tools/make_logo.py OUTPUT.png
    --font NotoSansJP.ttf --text-font NotoSansJP.ttf
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CAPTIONS = {
    "models_hint": "\u4eba\u7269\u30fb\u9854\u30fb\u9818\u57df\u3054\u3068\u306b\u63a8\u8ad6\u3092\u9078\u629e",
    "visualization_hint": "\u691c\u51fa\u67a0\u3068\u9818\u57df\u3092\u6620\u50cf\u306b\u91cd\u306d\u3066\u8868\u793a",
    "confidence_hint": "\u3057\u304d\u3044\u5024\u672a\u6e80\u306e\u691c\u51fa\u306f\u8868\u793a\u3057\u307e\u305b\u3093",
    "status_hint": "\u30ab\u30e1\u30e9\u753b\u9762\u306e\u30b9\u30c6\u30fc\u30bf\u30b9\u66f4\u65b0\u9593\u9694",
}


def load_font(path: Path, size: int, weight: int) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(path), size)
    try:
        axes = font.get_variation_axes()
    except OSError:
        return font
    font.set_variation_by_axes([
        min(max(weight, axis["minimum"]), axis["maximum"])
        if axis["name"] == b"Weight" else axis["default"] for axis in axes
    ])
    return font


def caption(text: str, font: ImageFont.FreeTypeFont) -> Image.Image:
    image = Image.new("RGBA", (352, 24))
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font, anchor="lt")
    if right - left > image.width or bottom - top > image.height:
        raise ValueError(f"caption does not fit: {text}")
    draw.text((0, (image.height - (bottom - top)) // 2), text,
              font=font, anchor="lt", fill="#A8B4BC")
    return image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--text-font", type=Path)
    options = parser.parse_args()
    logo_font = load_font(options.font, 52, 700)
    text_font_path = options.text_font or options.font
    text_font = load_font(text_font_path, 18, 500)
    logo = Image.new("RGBA", (152, 56))
    draw = ImageDraw.Draw(logo)
    draw.text((144, 3), "\u00b5AI", font=logo_font, anchor="rt", fill="#FFFFFF")
    images = {options.output: logo}
    images.update({options.output.parent / f"{name}.png": caption(text, text_font)
                   for name, text in CAPTIONS.items()})
    output = options.output
    output.parent.mkdir(parents=True, exist_ok=True)
    for path, image in images.items():
        image.save(path, format="PNG")
        print(f"wrote {path} ({image.width}x{image.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
