#!/usr/bin/env python3
"""
Generates bitmap font files (see generate.py) for a fixed set of glyph heights, from the same
monospace TTF.

Requires the freetype-py package: pip install freetype-py

Usage:
    python3 generate-all.py
"""

import importlib.util
import os

MONO_URL = "https://github.com/google/fonts/raw/refs/heads/main/ofl/ibmplexmono/IBMPlexMono-Regular.ttf"
SIZES = [12, 14, 16, 18, 24, 28]

spec = importlib.util.spec_from_file_location("generate_font", os.path.join(os.path.dirname(__file__), "generate.py"))
generate_font = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generate_font)


def write_fonts_header(outputs: list):
    path = os.path.join(generate_font.INCLUDE_DIR, "fonts.h")
    print(f"Generating {path}")
    with open(path, "w") as f:
        f.write("// SPDX-License-Identifier: Apache-2.0\n")
        f.write("#pragma once\n\n")
        f.write("// Every font generate-all.py produces, so a caller can pull them all in with one include.\n")
        for output in outputs:
            f.write(f"#include <font/{output}.h>\n")


if __name__ == "__main__":
    outputs = [f"ibmplexmono{size}" for size in SIZES]
    for output, size in zip(outputs, SIZES):
        generate_font.generate(MONO_URL, size, output)
    write_fonts_header(outputs)
