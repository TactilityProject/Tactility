#!/usr/bin/env python3
"""
Generates bitmap font files (see generate-font.py) for a fixed set of BDF fonts.

Usage:
    python3 generate-fonts.py
"""

import importlib.util
import os

BASE_URL = "https://github.com/metan-ucw/fonts/raw/refs/heads/master/"

FONTS = [
    (BASE_URL + "HaxorMedium-10.bdf", "haxormedium10"),
    (BASE_URL + "HaxorNarrow-18.bdf", "haxornarrow18"),
]

spec = importlib.util.spec_from_file_location("generate_font", os.path.join(os.path.dirname(__file__), "generate-font.py"))
generate_font = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generate_font)

if __name__ == "__main__":
    for url, output in FONTS:
        generate_font.generate(url, output)
