"""Minimal PNG reader/writer for layout images (standard library only).

Supports 8-bit non-interlaced PNGs with gray, gray+alpha, RGB, RGBA, and
palette color types. Pixels are returned as RGBA tuples.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


class PngError(ValueError):
    """Raised for unsupported or malformed PNG files."""


_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(data: bytes, width: int, height: int, bpp: int) -> bytearray:
    stride = width * bpp
    out = bytearray(stride * height)
    previous = bytearray(stride)
    offset = 0
    for row in range(height):
        if offset >= len(data):
            raise PngError("truncated image data")
        filter_type = data[offset]
        offset += 1
        line = bytearray(data[offset:offset + stride])
        if len(line) != stride:
            raise PngError("truncated image data")
        offset += stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = previous[i]
            c = previous[i - bpp] if i >= bpp else 0
            if filter_type == 0:
                value = line[i]
            elif filter_type == 1:
                value = line[i] + a
            elif filter_type == 2:
                value = line[i] + b
            elif filter_type == 3:
                value = line[i] + ((a + b) >> 1)
            elif filter_type == 4:
                value = line[i] + _paeth(a, b, c)
            else:
                raise PngError(f"unknown filter type {filter_type}")
            line[i] = value & 0xFF
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return out


def read_png(path: Path) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    """Return (width, height, rgba pixels in row-major order)."""
    data = path.read_bytes()
    if not data.startswith(_SIGNATURE):
        raise PngError(f"{path} is not a PNG file")
    offset = len(_SIGNATURE)
    width = height = 0
    bit_depth = color_type = interlace = 0
    palette: list[tuple[int, int, int]] = []
    transparency: bytes = b""
    idat = bytearray()
    while offset + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk)
        elif kind == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]  # type: ignore[misc]
        elif kind == b"tRNS":
            transparency = bytes(chunk)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    if width == 0 or height == 0:
        raise PngError(f"{path}: missing IHDR")
    if bit_depth != 8 or interlace != 0 or color_type not in _CHANNELS:
        raise PngError(
            f"{path}: only 8-bit non-interlaced gray/RGB/RGBA/palette PNGs are supported")
    bpp = _CHANNELS[color_type]
    raw = _unfilter(zlib.decompress(bytes(idat)), width, height, bpp)
    pixels: list[tuple[int, int, int, int]] = []
    for i in range(0, len(raw), bpp):
        if color_type == 0:
            g = raw[i]
            pixels.append((g, g, g, 255))
        elif color_type == 4:
            g = raw[i]
            pixels.append((g, g, g, raw[i + 1]))
        elif color_type == 2:
            pixels.append((raw[i], raw[i + 1], raw[i + 2], 255))
        elif color_type == 6:
            pixels.append((raw[i], raw[i + 1], raw[i + 2], raw[i + 3]))
        else:
            index = raw[i]
            if index >= len(palette):
                raise PngError(f"{path}: palette index out of range")
            alpha = transparency[index] if index < len(transparency) else 255
            pixels.append((*palette[index], alpha))
    return width, height, pixels


def write_png(path: Path, width: int, height: int,
              rgba: list[tuple[int, int, int, int]]) -> None:
    raw = bytearray()
    for row in range(height):
        raw.append(0)
        for column in range(width):
            raw.extend(rgba[row * width + column])

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(_SIGNATURE + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
