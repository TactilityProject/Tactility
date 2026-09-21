#!/usr/bin/env python3
"""
Downloads a BDF bitmap font and converts it into the raw glyph bitmap format the terminal app's
TerminalRenderer expects: one C array covering U+0020-U+007E, height rows per glyph, each row
ceil(width/8) bytes (bit 7 of the first byte = leftmost pixel), plus a header declaring it.

Glyph width/height are taken from the font's own FONTBOUNDINGBOX - a BDF is a fixed-size bitmap
font, so unlike a TTF there is no size to choose.

Usage:
    python3 generate-font.py --url URL --output NAME
"""

import argparse
import math
import os
import urllib.request

GLYPH_FIRST = 0x20
GLYPH_LAST = 0x7E

# Generated font files land here, next to vterm.c.
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Tactility", "Source", "app", "terminal", "vterm")


def download_file(url: str, filename: str):
    if not os.path.exists(filename):
        print(f"Downloading {filename} from {url}")
        urllib.request.urlretrieve(url, filename)
    else:
        print(f"{filename} already exists, skipping download.")


def parse_bdf(path: str):
    """
    Returns (fbb_width, fbb_height, fbb_xoff, fbb_yoff, glyphs), where glyphs maps a codepoint to
    (bbx_width, bbx_height, bbx_xoff, bbx_yoff, rows) and rows is a list of ints (one per bitmap
    row, top row first), each holding that row's bytes as a single big-endian value.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip("\n") for line in f]

    fbb = None
    glyphs = {}
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("FONTBOUNDINGBOX"):
            fbb = tuple(int(x) for x in line.split()[1:5])
        elif line.startswith("STARTCHAR"):
            encoding = None
            bbx = None
            rows = []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                if lines[i].startswith("ENCODING"):
                    encoding = int(lines[i].split()[1])
                elif lines[i].startswith("BBX"):
                    bbx = tuple(int(x) for x in lines[i].split()[1:5])
                elif lines[i].startswith("BITMAP"):
                    i += 1
                    while not lines[i].startswith("ENDCHAR"):
                        rows.append(int(lines[i].strip(), 16))
                        i += 1
                    continue
                i += 1
            if encoding is not None and bbx is not None:
                glyphs[encoding] = bbx + (rows,)
        i += 1

    if fbb is None:
        raise ValueError(f"{path}: missing FONTBOUNDINGBOX")
    return fbb, glyphs


def get_glyph_bit(row: int, bytes_per_row: int, col: int) -> bool:
    total_bits = bytes_per_row * 8
    if col < 0 or col >= total_bits:
        return False
    return bool((row >> (total_bits - 1 - col)) & 1)


def render_glyph(fbb, bbx_glyph, cell_width: int, cell_height: int, bytes_per_row: int) -> bytearray:
    """Places a glyph's own BBX-relative bitmap into a fixed cell_width x cell_height grid,
    aligned by baseline using the font's FONTBOUNDINGBOX and the glyph's own BBX offsets."""
    fbb_width, fbb_height, fbb_xoff, fbb_yoff = fbb
    bbx_width, bbx_height, bbx_xoff, bbx_yoff, rows = bbx_glyph
    src_bytes_per_row = math.ceil(bbx_width / 8) if bbx_width > 0 else 0

    out = bytearray(cell_height * bytes_per_row)
    for r, row in enumerate(rows):
        y = (bbx_yoff + bbx_height - 1) - r
        dest_row = (fbb_yoff + fbb_height - 1) - y
        if not 0 <= dest_row < cell_height:
            continue
        for c in range(bbx_width):
            x = bbx_xoff + c
            dest_col = x - fbb_xoff
            if not 0 <= dest_col < cell_width:
                continue
            if get_glyph_bit(row, src_bytes_per_row, c):
                out[dest_row * bytes_per_row + dest_col // 8] |= 1 << (7 - dest_col % 8)
    return out


def escape_char(ch: str) -> str:
    if ch == "\\":
        return "\\\\"
    if ch == '"':
        return '\\"'
    return ch


def write_c_file(path: str, array_name: str, width: int, height: int, bytes_per_row: int, glyphs: dict):
    glyph_count = GLYPH_LAST - GLYPH_FIRST + 1
    glyph_bytes = height * bytes_per_row
    total_bytes = glyph_count * glyph_bytes
    with open(path, "w") as f:
        f.write(f"/*\n * {os.path.basename(path)} - {array_name} Bitmap Data\n")
        f.write(f" *\n * Converted from a BDF font with Buildscripts/generate-font.py.\n")
        f.write(f" * Each glyph is {width} pixels wide and {height} pixels tall, stored as {height} rows of\n")
        f.write(f" * {bytes_per_row} byte{'s' if bytes_per_row != 1 else ''} each (bit 7 of the first byte = leftmost pixel).\n */\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"// Glyph bitmap data: {glyph_count} characters (0x20-0x7E), {glyph_bytes} bytes each = {total_bytes} bytes\n")
        f.write(f"// Access pattern: {array_name}[(char_code - 0x20) * {glyph_bytes} + row * {bytes_per_row} + byte]\n")
        f.write(f"const uint8_t {array_name}[] = {{\n")
        for cp in range(GLYPH_FIRST, GLYPH_LAST + 1):
            f.write(f'    /* U+{cp:04X} "{escape_char(chr(cp))}" */\n')
            data = glyphs[cp]
            f.write("    " + ", ".join(f"0x{b:X}" for b in data) + ",\n")
        f.write("};\n")


def write_h_file(path: str, array_name: str, macro_prefix: str, width: int, height: int, bytes_per_row: int):
    with open(path, "w") as f:
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define {macro_prefix}_GLYPH_WIDTH {width}\n")
        f.write(f"#define {macro_prefix}_GLYPH_HEIGHT {height}\n")
        f.write(f"#define {macro_prefix}_GLYPH_BYTES_PER_ROW {bytes_per_row}\n")
        f.write(f"#define {macro_prefix}_GLYPH_FIRST 0x{GLYPH_FIRST:02X}\n")
        f.write(f"#define {macro_prefix}_GLYPH_LAST 0x{GLYPH_LAST:02X}\n\n")
        f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        f.write(f"extern const uint8_t {array_name}[];\n\n")
        f.write("#ifdef __cplusplus\n}\n#endif\n")


def generate(bdf_url: str, output: str):
    bdf_filename = os.path.basename(bdf_url)
    download_file(bdf_url, bdf_filename)

    fbb, bdf_glyphs = parse_bdf(bdf_filename)
    width, height = fbb[0], fbb[1]
    bytes_per_row = math.ceil(width / 8)

    glyphs = {}
    for cp in range(GLYPH_FIRST, GLYPH_LAST + 1):
        if cp not in bdf_glyphs:
            print(f"Warning: U+{cp:04X} not found in {bdf_filename}, leaving blank")
            glyphs[cp] = bytearray(height * bytes_per_row)
            continue
        glyphs[cp] = render_glyph(fbb, bdf_glyphs[cp], width, height, bytes_per_row)

    array_name = f"{output}_glyph_bitmap"
    macro_prefix = output.upper()

    c_path = os.path.join(OUTPUT_DIR, f"{output}.c")
    h_path = os.path.join(OUTPUT_DIR, f"{output}.h")
    print(f"Generating {c_path}")
    write_c_file(c_path, array_name, width, height, bytes_per_row, glyphs)
    print(f"Generating {h_path}")
    write_h_file(h_path, array_name, macro_prefix, width, height, bytes_per_row)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="URL of the BDF font to download")
    parser.add_argument("--output", required=True, help="Output file base name (no extension)")
    args = parser.parse_args()

    generate(args.url, args.output)
