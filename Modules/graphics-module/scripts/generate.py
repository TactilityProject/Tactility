#!/usr/bin/env python3
"""
Downloads a monospace TTF and rasterizes it into a FixedWidthFont (see include/font/font.h) covering:

- U+0020-U+007E
- Height rows per glyph
- Each row ceil(width / 8)
- Bytes (bit 7 of the first byte = leftmost pixel)
Plus a header declaring the font. Glyph width is the font's own natural advance width at the given size.

Requires the freetype-py package: pip install freetype-py

Usage:
    python3 generate.py --url URL --size SIZE --output NAME
"""

import argparse
import math
import os
import re
import urllib.request

import freetype

GLYPH_FIRST = 0x20
GLYPH_LAST = 0x7E

MODULE_DIR = os.path.join(os.path.dirname(__file__), "..")
SOURCE_DIR = os.path.join(MODULE_DIR, "source")
INCLUDE_DIR = os.path.join(MODULE_DIR, "include", "font")


def download_file(url: str, filename: str):
    if not os.path.exists(filename):
        print(f"Downloading {filename} from {url}")
        urllib.request.urlretrieve(url, filename)
    else:
        print(f"{filename} already exists, skipping download.")


def measure_advance_width(face: freetype.Face, size: int) -> float:
    """Natural advance width (px) of the font at `size` px tall, isotropic DPI."""
    face.set_char_size(0, size * 64, 72, 72)
    face.load_char("M", freetype.FT_LOAD_DEFAULT)
    return face.glyph.advance.x / 64


def get_bit(bitmap, x: int, y: int) -> bool:
    if x < 0 or x >= bitmap.width or y < 0 or y >= bitmap.rows:
        return False
    byte = bitmap.buffer[y * abs(bitmap.pitch) + x // 8]
    return bool((byte >> (7 - x % 8)) & 1)


def measure_baseline(face: freetype.Face, cell_height: int) -> int:
    """
    Row (from the cell top) the baseline sits on.

    face.size.ascender/descender are line-spacing metrics, not ink extents - many fonts set them
    taller than the glyphs actually are, which pushes the baseline row below the bottom of the
    cell and clips it off entirely. Measuring the real cap-height ('A') and descender depth ('g')
    at this pixel size keeps both in view instead.
    """
    face.load_char("A", freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    cap_top = face.glyph.bitmap_top
    face.load_char("g", freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    descender = face.glyph.bitmap.rows - face.glyph.bitmap_top

    max_baseline = cell_height - 1 - descender
    if max_baseline >= cap_top:
        baseline = cap_top + (max_baseline - cap_top) // 2
    else:
        # Cap height and descender depth don't both fit in cell_height rows (common at very
        # small sizes) - keep the cap/baseline row intact and let the descender clip instead.
        baseline = cap_top
    return min(max(baseline, 0), cell_height - 1)


def render_glyph(face: freetype.Face, codepoint: int, cell_width: int, cell_height: int, baseline_y: int, bytes_per_row: int) -> bytearray:
    face.load_char(chr(codepoint), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    glyph = face.glyph
    bitmap = glyph.bitmap

    out = bytearray(cell_height * bytes_per_row)
    for row in range(cell_height):
        src_y = row - baseline_y + glyph.bitmap_top
        for col in range(cell_width):
            src_x = col - glyph.bitmap_left
            if get_bit(bitmap, src_x, src_y):
                out[row * bytes_per_row + col // 8] |= 1 << (7 - col % 8)
    return out


def split_name(output: str) -> tuple:
    """'ibmplexmono14' -> ('ibmplexmono', '14'), so identifiers can put an underscore between the
    font name and its size instead of running them together."""
    match = re.match(r"^([a-zA-Z]+)(\d+)$", output)
    if not match:
        raise ValueError(f"'{output}' must be a name followed by digits, e.g. 'ibmplexmono14'")
    return match.group(1), match.group(2)


def escape_char(ch: str) -> str:
    if ch == "\\":
        return "\\\\"
    if ch == '"':
        return '\\"'
    return ch


def write_c_file(path: str, output: str, width: int, height: int, bytes_per_row: int, glyphs: dict):
    glyph_count = GLYPH_LAST - GLYPH_FIRST + 1
    glyph_bytes = height * bytes_per_row
    total_bytes = glyph_count * glyph_bytes
    name, size_digits = split_name(output)
    bitmap_name = f"{output}_glyph_bitmap"
    struct_name = f"{name}_{size_digits}_font"
    config_macro = f"CONFIG_TT_FONT_{name.upper()}_{size_digits}"
    with open(path, "w") as f:
        f.write(f"/*\n * {os.path.basename(path)} - {output} FixedWidthFont\n")
        f.write(f" *\n * Rasterized from a TTF with Modules/graphics-module/scripts/generate.py.\n")
        f.write(f" * Each glyph is {width} pixels wide and {height} pixels tall, stored as {height} rows of\n")
        f.write(f" * {bytes_per_row} byte{'s' if bytes_per_row != 1 else ''} each (bit 7 of the first byte = leftmost pixel).\n */\n\n")
        f.write(f"#include <font/{output}.h>\n\n")
        f.write("#ifdef ESP_PLATFORM\n")
        f.write("#include <sdkconfig.h>\n")
        f.write("#endif\n\n")
        f.write(f"// See Modules/graphics-module/Kconfig: undefined (POSIX, or an ESP-IDF build predating\n")
        f.write(f"// this font's Kconfig entry) is treated the same as enabled.\n")
        f.write(f"#if !defined({config_macro}) || {config_macro}\n\n")
        f.write(f"// Glyph bitmap data: {glyph_count} characters (0x20-0x7E), {glyph_bytes} bytes each = {total_bytes} bytes\n")
        f.write(f"static const uint8_t {bitmap_name}[] = {{\n")
        for cp in range(GLYPH_FIRST, GLYPH_LAST + 1):
            f.write(f'    /* U+{cp:04X} "{escape_char(chr(cp))}" */\n')
            data = glyphs[cp]
            f.write("    " + ", ".join(f"0x{b:X}" for b in data) + ",\n")
        f.write("};\n\n")
        f.write(f"const FixedWidthFont {struct_name} = {{\n")
        f.write(f"    .glyph_width = {width},\n")
        f.write(f"    .glyph_height = {height},\n")
        f.write(f"    .glyph_bytes_per_row = {bytes_per_row},\n")
        f.write(f"    .first_codepoint = 0x{GLYPH_FIRST:02X},\n")
        f.write(f"    .last_codepoint = 0x{GLYPH_LAST:02X},\n")
        f.write(f"    .glyph_bitmap = {bitmap_name},\n")
        f.write("};\n\n")
        f.write(f"#endif // {config_macro}\n")


def write_h_file(path: str, output: str):
    name, size_digits = split_name(output)
    struct_name = f"{name}_{size_digits}_font"
    with open(path, "w") as f:
        f.write("// SPDX-License-Identifier: Apache-2.0\n")
        f.write("#pragma once\n\n")
        f.write("#include <font/font.h>\n\n")
        f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        f.write(f"extern const FixedWidthFont {struct_name};\n\n")
        f.write("#ifdef __cplusplus\n}\n#endif\n")


def generate(mono_url: str, size: int, output: str):
    ttf_filename = os.path.basename(mono_url)
    download_file(mono_url, ttf_filename)

    face = freetype.Face(ttf_filename)
    natural_width = measure_advance_width(face, size)
    if natural_width <= 0:
        raise RuntimeError("Font reported a non-positive advance width")
    width = round(natural_width)

    bytes_per_row = math.ceil(width / 8)
    baseline_y = measure_baseline(face, size)
    glyphs = {cp: render_glyph(face, cp, width, size, baseline_y, bytes_per_row) for cp in range(GLYPH_FIRST, GLYPH_LAST + 1)}

    c_path = os.path.join(SOURCE_DIR, f"{output}.c")
    h_path = os.path.join(INCLUDE_DIR, f"{output}.h")
    print(f"Generating {h_path}")
    write_h_file(h_path, output)
    print(f"Generating {c_path}")
    write_c_file(c_path, output, width, size, bytes_per_row, glyphs)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="URL of the monospace TTF to download")
    parser.add_argument("--size", type=int, required=True, help="Glyph height in pixels")
    parser.add_argument("--output", required=True, help="Output file base name (no extension)")
    args = parser.parse_args()

    generate(args.url, args.size, args.output)
