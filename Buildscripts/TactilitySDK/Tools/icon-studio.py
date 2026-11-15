#!/usr/bin/env python3
"""Tactility Icon Studio - build `icon.svg` and `icon_mono.svg` from one SVG glyph.

Paste an SVG downloaded from Material Symbols (or any other icon set), pick a
glyph from the built-in searchable Material Icons browser, tune the colours and
glyph size, watch a live preview at launcher size, and write both icons into an
app folder.

The whole pipeline is self contained: the SVG is parsed and rendered by this
file, so the preview shows exactly the geometry that ends up in the two output
files. Previews need Pillow (already part of Custom2kinter); the Material Icons
browser turns font outlines into SVG with fontTools (`pip install fonttools`).

Run it with:
    python Scripts/icon-studio.py

Headless use (handy for CI or a quick one-off):
    python Scripts/icon-studio.py --no-gui --svg glyph.svg --out Apps/MyApp
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
import queue
import re
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - covered by the Custom2kinter install hint
    Image = ImageChops = ImageDraw = ImageFont = None

try:
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.ttLib import TTFont
except ImportError:  # pragma: no cover - only the Material Icons browser needs it
    BoundsPen = SVGPathPen = TTFont = None

try:
    import tkinter as tk
    from tkinter import colorchooser, filedialog, messagebox
except ImportError:  # pragma: no cover - Linux without python3-tk
    tk = colorchooser = filedialog = messagebox = None

try:
    # The maintained fork is published on PyPI as "custom2kinter"; it still
    # provides the drop-in `customtkinter` module name.
    import customtkinter as ctk
except ImportError:  # pragma: no cover - dependency hint, reported on launch
    ctk = None


APP_NAME = "Tactility Icon Studio"
SVG_NS = "http://www.w3.org/2000/svg"
XLINK_NS = "http://www.w3.org/1999/xlink"

CANVAS = 96
INK = "#000000"
SUPERSAMPLE = 4
PREVIEW_SIZE = 288
PREVIEW_RENDER = 384
SMALL_SIZE = 24

DEFAULT_BG = "#0EA5E9"
DEFAULT_GLYPH = "#FFFFFF"
DEFAULT_COLOUR_SIZE = 72.0
DEFAULT_MONO_SIZE = 80.0
DEFAULT_MIN_STROKE = 3.5

GLYPH_MODES = {
    "Auto contrast": "auto",
    "Solid colour": "solid",
    "Keep source colours": "source",
}
GLYPH_MODE_LABELS = {value: label for label, value in GLYPH_MODES.items()}

BG_PALETTE = [
    "#0EA5E9", "#2563EB", "#7C3AED", "#EC4899",
    "#EF4444", "#E7352C", "#F97316", "#FACC15",
    "#4ADE80", "#14B8A6", "#0F172A", "#1E293B",
    "#64748B", "#FFFFFF",
]

DARK_CARD = "#101016"
LIGHT_CARD = "#EEF0F4"
DARK_INK = "#E9E9F2"
LIGHT_INK = "#17171D"

MATERIAL_ICONS_BASE = "https://raw.githubusercontent.com/google/material-design-icons/master/font/"
MATERIAL_STYLES = {
    "Filled": ("MaterialIcons-Regular.ttf", "MaterialIcons-Regular.codepoints"),
    "Outlined": ("MaterialIconsOutlined-Regular.otf", "MaterialIconsOutlined-Regular.codepoints"),
    "Round": ("MaterialIconsRound-Regular.otf", "MaterialIconsRound-Regular.codepoints"),
    "Sharp": ("MaterialIconsSharp-Regular.otf", "MaterialIconsSharp-Regular.codepoints"),
}
MATERIAL_LICENSE_URL = "https://github.com/google/material-design-icons/blob/master/LICENSE"
MATERIAL_SITE_URL = "https://fonts.google.com/icons?icon.set=Material+Icons"


class IconError(Exception):
    """A problem with the source SVG or the requested output."""


# ---------------------------------------------------------------------------
# Numbers, matrices, colours
# ---------------------------------------------------------------------------
def clamp(value: float, low: float, high: float) -> float:
    return low if value < low else high if value > high else value


def fmt(value: float) -> str:
    """Format a number for SVG: at most 2 decimals, no trailing zeros."""
    if abs(value) < 0.005:
        return "0"
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return text or "0"


NUMBER_RE = re.compile(r"[+-]?(?:(?:\d+\.\d*)|(?:\.\d+)|(?:\d+))(?:[eE][+-]?\d+)?")


def as_float(value, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def parse_length(value, reference: float, default: float = 0.0) -> float:
    if value is None:
        return default
    text = str(value).strip()
    if not text:
        return default
    try:
        if text.endswith("%"):
            return as_float(text[:-1]) / 100.0 * reference
        match = NUMBER_RE.match(text)
        return float(match.group(0)) if match else default
    except (TypeError, ValueError):
        return default


# Affine matrix (a, b, c, d, e, f): x' = a*x + c*y + e, y' = b*x + d*y + f.
Matrix = tuple[float, float, float, float, float, float]
IDENTITY: Matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mat_mul(left: Matrix, right: Matrix) -> Matrix:
    a1, b1, c1, d1, e1, f1 = left
    a2, b2, c2, d2, e2, f2 = right
    return (
        a1 * a2 + c1 * b2,
        b1 * a2 + d1 * b2,
        a1 * c2 + c1 * d2,
        b1 * c2 + d1 * d2,
        a1 * e2 + c1 * f2 + e1,
        b1 * e2 + d1 * f2 + f1,
    )


def mat_apply(matrix: Matrix, x: float, y: float) -> tuple[float, float]:
    a, b, c, d, e, f = matrix
    return a * x + c * y + e, b * x + d * y + f


def mat_scale(matrix: Matrix) -> float:
    a, b, c, d, _e, _f = matrix
    return math.sqrt(abs(a * d - b * c)) or 1.0


def mat_is_identity(matrix: Matrix) -> bool:
    return all(abs(a - b) < 1e-9 for a, b in zip(matrix, IDENTITY))


TRANSFORM_RE = re.compile(r"([a-zA-Z]+)\s*\(([^)]*)\)")


def parse_transform(text: str | None) -> Matrix:
    if not text:
        return IDENTITY
    result = IDENTITY
    for name, raw in TRANSFORM_RE.findall(text):
        values = [float(match.group(0)) for match in NUMBER_RE.finditer(raw)]
        name = name.lower()
        if name == "matrix" and len(values) >= 6:
            current = tuple(values[:6])  # type: ignore[assignment]
        elif name == "translate" and values:
            current = (1.0, 0.0, 0.0, 1.0, values[0], values[1] if len(values) > 1 else 0.0)
        elif name == "scale" and values:
            sx = values[0]
            sy = values[1] if len(values) > 1 else sx
            current = (sx, 0.0, 0.0, sy, 0.0, 0.0)
        elif name == "rotate" and values:
            angle = math.radians(values[0])
            cos_a, sin_a = math.cos(angle), math.sin(angle)
            rotation: Matrix = (cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0)
            if len(values) >= 3:
                cx, cy = values[1], values[2]
                rotation = mat_mul(
                    (1.0, 0.0, 0.0, 1.0, cx, cy),
                    mat_mul(rotation, (1.0, 0.0, 0.0, 1.0, -cx, -cy)),
                )
            current = rotation
        elif name == "skewx" and values:
            current = (1.0, 0.0, math.tan(math.radians(values[0])), 1.0, 0.0, 0.0)
        elif name == "skewy" and values:
            current = (1.0, math.tan(math.radians(values[0])), 0.0, 1.0, 0.0, 0.0)
        else:
            continue
        result = mat_mul(result, current)
    return result


@dataclass(frozen=True)
class RGBA:
    r: int
    g: int
    b: int
    a: int = 255

    @property
    def hex(self) -> str:
        return f"#{self.r:02X}{self.g:02X}{self.b:02X}"

    @property
    def tuple(self) -> tuple[int, int, int, int]:
        return self.r, self.g, self.b, self.a

    def with_alpha(self, alpha: int) -> "RGBA":
        return RGBA(self.r, self.g, self.b, clamp(alpha, 0, 255))


NAMED_COLOURS = {
    "black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0),
    "lime": (0, 255, 0), "green": (0, 128, 0), "blue": (0, 0, 255),
    "yellow": (255, 255, 0), "cyan": (0, 255, 255), "aqua": (0, 255, 255),
    "magenta": (255, 0, 255), "fuchsia": (255, 0, 255), "gray": (128, 128, 128),
    "grey": (128, 128, 128), "silver": (192, 192, 192), "maroon": (128, 0, 0),
    "olive": (128, 128, 0), "purple": (128, 0, 128), "teal": (0, 128, 128),
    "navy": (0, 0, 128), "orange": (255, 165, 0), "pink": (255, 192, 203),
    "brown": (165, 42, 42), "gold": (255, 215, 0), "indigo": (75, 0, 130),
    "violet": (238, 130, 238), "transparent": (0, 0, 0),
}


def parse_color(value: str | None, current: str = "black") -> RGBA | None:
    """Parse the colour notations that appear in icon SVGs."""
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text or text == "none" or text == "transparent":
        return None
    if text == "currentcolor":
        text = (current or "black").strip().lower()

    if text.startswith("#"):
        digits = text[1:]
        try:
            if len(digits) == 3:
                return RGBA(*(int(c * 2, 16) for c in digits), 255)
            if len(digits) == 4:
                return RGBA(*(int(c * 2, 16) for c in digits[:3]), int(digits[3] * 2, 16))
            if len(digits) == 6:
                return RGBA(int(digits[0:2], 16), int(digits[2:4], 16), int(digits[4:6], 16), 255)
            if len(digits) == 8:
                return RGBA(int(digits[0:2], 16), int(digits[2:4], 16),
                            int(digits[4:6], 16), int(digits[6:8], 16))
        except ValueError:
            return None

    match = re.match(r"rgba?\(([^)]*)\)", text)
    if match:
        parts = [part for part in re.split(r"[,\s/]+", match.group(1).strip()) if part]
        if len(parts) >= 3:
            def channel(part: str) -> int:
                return round(as_float(part[:-1]) * 255 / 100) if part.endswith("%") else round(as_float(part))
            alpha = 255
            if len(parts) >= 4:
                alpha = round(as_float(parts[3][:-1]) * 255 / 100) if parts[3].endswith("%") else round(as_float(parts[3]) * 255)
            return RGBA(clamp(channel(parts[0]), 0, 255), clamp(channel(parts[1]), 0, 255),
                        clamp(channel(parts[2]), 0, 255), clamp(alpha, 0, 255))

    match = re.match(r"hsla?\(([^)]*)\)", text)
    if match:
        import colorsys

        parts = [part for part in re.split(r"[,\s/]+", match.group(1).strip()) if part]
        if len(parts) >= 3:
            hue = as_float(parts[0].rstrip("deg")) / 360.0
            saturation = as_float(parts[1].rstrip("%")) / 100.0
            lightness = as_float(parts[2].rstrip("%")) / 100.0
            red, green, blue = colorsys.hls_to_rgb(hue % 1.0, lightness, saturation)
            alpha = 255
            if len(parts) >= 4:
                alpha = round(as_float(parts[3].rstrip("%")) * (2.55 if parts[3].endswith("%") else 255))
            return RGBA(round(red * 255), round(green * 255), round(blue * 255), clamp(alpha, 0, 255))

    if text in NAMED_COLOURS:
        red, green, blue = NAMED_COLOURS[text]
        return RGBA(red, green, blue, 255)
    return None


def normalize_hex(value: str | None, fallback: str = DEFAULT_BG) -> str:
    parsed = parse_color(value)
    return parsed.hex if parsed is not None else fallback


def relative_luminance(rgba: RGBA) -> float:
    def linear(channel: int) -> float:
        value = channel / 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4

    return 0.2126 * linear(rgba.r) + 0.7152 * linear(rgba.g) + 0.0722 * linear(rgba.b)


def contrast_ratio(first: RGBA, second: RGBA) -> float:
    a = relative_luminance(first)
    b = relative_luminance(second)
    high, low = max(a, b), min(a, b)
    return (high + 0.05) / (low + 0.05)


def auto_glyph_colour(background: str) -> str:
    parsed = parse_color(background) or RGBA(255, 255, 255)
    return "#000000" if relative_luminance(parsed) > 0.42 else "#FFFFFF"


# ---------------------------------------------------------------------------
# Path data
# ---------------------------------------------------------------------------
@dataclass
class Subpath:
    points: list[tuple[float, float]]
    closed: bool = False


class PathScanner:
    def __init__(self, text: str) -> None:
        self.text = text or ""
        self.index = 0
        self.length = len(self.text)

    def skip(self) -> None:
        while self.index < self.length and self.text[self.index] in " \t\r\n,":
            self.index += 1

    def at_end(self) -> bool:
        self.skip()
        return self.index >= self.length

    def command(self) -> str | None:
        self.skip()
        if self.index < self.length and self.text[self.index].isalpha():
            value = self.text[self.index]
            self.index += 1
            return value
        return None

    def number(self) -> float:
        self.skip()
        match = NUMBER_RE.match(self.text, self.index)
        if match is None:
            snippet = self.text[self.index:self.index + 16]
            raise IconError(f"Invalid path data near {snippet!r}")
        self.index = match.end()
        return float(match.group(0))

    def flag(self) -> bool:
        self.skip()
        if self.index >= self.length or self.text[self.index] not in "01":
            raise IconError("Invalid arc flag in path data")
        value = self.text[self.index] == "1"
        self.index += 1
        return value


def flatten_cubic(start, control1, control2, end, out: list[tuple[float, float]]) -> None:
    length = (math.dist(start, control1) + math.dist(control1, control2)
              + math.dist(control2, end))
    steps = int(clamp(round(length / 12.0), 6, 64))
    x0, y0 = start
    for step in range(1, steps + 1):
        t = step / steps
        u = 1.0 - t
        x = u * u * u * x0 + 3 * u * u * t * control1[0] + 3 * u * t * t * control2[0] + t * t * t * end[0]
        y = u * u * u * y0 + 3 * u * u * t * control1[1] + 3 * u * t * t * control2[1] + t * t * t * end[1]
        out.append((x, y))


def flatten_quadratic(start, control, end, out: list[tuple[float, float]]) -> None:
    length = math.dist(start, control) + math.dist(control, end)
    steps = int(clamp(round(length / 10.0), 5, 48))
    x0, y0 = start
    for step in range(1, steps + 1):
        t = step / steps
        u = 1.0 - t
        out.append((u * u * x0 + 2 * u * t * control[0] + t * t * end[0],
                    u * u * y0 + 2 * u * t * control[1] + t * t * end[1]))


def arc_to_cubics(start, rx, ry, angle, large_arc, sweep, end):
    """SVG endpoint arc -> list of (control1, control2, end) cubic segments."""
    x1, y1 = start
    x2, y2 = end
    if abs(x1 - x2) < 1e-9 and abs(y1 - y2) < 1e-9:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx < 1e-9 or ry < 1e-9:
        return None

    phi = math.radians(angle % 360.0)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy

    correction = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if correction > 1.0:
        scale = math.sqrt(correction)
        rx *= scale
        ry *= scale

    numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coefficient = 0.0
    if denominator > 0:
        coefficient = math.sqrt(max(0.0, numerator / denominator))
        if large_arc == sweep:
            coefficient = -coefficient

    cxp = coefficient * rx * y1p / ry
    cyp = -coefficient * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0

    def vector_angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        norm = math.hypot(ux, uy) * math.hypot(vx, vy)
        value = math.acos(clamp(dot / norm, -1.0, 1.0)) if norm else 0.0
        return -value if ux * vy - uy * vx < 0 else value

    start_angle = vector_angle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    delta = vector_angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                         (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi

    count = max(1, int(math.ceil(abs(delta) / (math.pi / 2.0))))
    step = delta / count
    segments = []
    for index in range(count):
        a1 = start_angle + index * step
        a2 = a1 + step
        alpha = 4.0 / 3.0 * math.tan(step / 4.0)

        def point(a):
            return (cx + rx * math.cos(a) * cos_phi - ry * math.sin(a) * sin_phi,
                    cy + rx * math.cos(a) * sin_phi + ry * math.sin(a) * cos_phi)

        p1 = point(a1)
        p2 = point(a2)
        d1 = (-rx * math.sin(a1) * cos_phi - ry * math.cos(a1) * sin_phi,
              -rx * math.sin(a1) * sin_phi + ry * math.cos(a1) * cos_phi)
        d2 = (-rx * math.sin(a2) * cos_phi - ry * math.cos(a2) * sin_phi,
              -rx * math.sin(a2) * sin_phi + ry * math.cos(a2) * cos_phi)
        segments.append(((p1[0] + alpha * d1[0], p1[1] + alpha * d1[1]),
                         (p2[0] - alpha * d2[0], p2[1] - alpha * d2[1]),
                         p2))
    return segments


def parse_path_data(text: str | None) -> list[Subpath]:
    if not text:
        return []
    scanner = PathScanner(text)
    subpaths: list[Subpath] = []
    points: list[tuple[float, float]] = []
    position = (0.0, 0.0)
    start = (0.0, 0.0)
    command: str | None = None
    last_cubic: tuple[float, float] | None = None
    last_quad: tuple[float, float] | None = None

    def flush(closed: bool = False) -> None:
        nonlocal points
        if len(points) >= 2:
            subpaths.append(Subpath(points, closed))
        points = []

    def ensure_start() -> None:
        # A drawing command after Z starts at the closed subpath's initial point.
        nonlocal points
        if not points:
            points = [position]

    while not scanner.at_end():
        token = scanner.command()
        if token is not None:
            command = token
        elif command is None:
            raise IconError("Path data has coordinates without a command")

        relative = command.islower()
        kind = command.upper()

        if kind == "M":
            x, y = scanner.number(), scanner.number()
            if relative:
                x, y = x + position[0], y + position[1]
            flush()
            position = start = (x, y)
            points = [position]
            command = "l" if relative else "L"
            last_cubic = last_quad = None
        elif kind == "L":
            x, y = scanner.number(), scanner.number()
            if relative:
                x, y = x + position[0], y + position[1]
            ensure_start()
            position = (x, y)
            points.append(position)
            last_cubic = last_quad = None
        elif kind == "H":
            x = scanner.number()
            if relative:
                x += position[0]
            ensure_start()
            position = (x, position[1])
            points.append(position)
            last_cubic = last_quad = None
        elif kind == "V":
            y = scanner.number()
            if relative:
                y += position[1]
            ensure_start()
            position = (position[0], y)
            points.append(position)
            last_cubic = last_quad = None
        elif kind == "C":
            x1, y1 = scanner.number(), scanner.number()
            x2, y2 = scanner.number(), scanner.number()
            x, y = scanner.number(), scanner.number()
            if relative:
                x1, y1 = x1 + position[0], y1 + position[1]
                x2, y2 = x2 + position[0], y2 + position[1]
                x, y = x + position[0], y + position[1]
            ensure_start()
            flatten_cubic(position, (x1, y1), (x2, y2), (x, y), points)
            last_cubic = (x2, y2)
            last_quad = None
            position = (x, y)
        elif kind == "S":
            x2, y2 = scanner.number(), scanner.number()
            x, y = scanner.number(), scanner.number()
            if relative:
                x2, y2 = x2 + position[0], y2 + position[1]
                x, y = x + position[0], y + position[1]
            if last_cubic is None:
                x1, y1 = position
            else:
                x1, y1 = 2 * position[0] - last_cubic[0], 2 * position[1] - last_cubic[1]
            ensure_start()
            flatten_cubic(position, (x1, y1), (x2, y2), (x, y), points)
            last_cubic = (x2, y2)
            last_quad = None
            position = (x, y)
        elif kind == "Q":
            qx, qy = scanner.number(), scanner.number()
            x, y = scanner.number(), scanner.number()
            if relative:
                qx, qy = qx + position[0], qy + position[1]
                x, y = x + position[0], y + position[1]
            ensure_start()
            flatten_quadratic(position, (qx, qy), (x, y), points)
            last_quad = (qx, qy)
            last_cubic = None
            position = (x, y)
        elif kind == "T":
            x, y = scanner.number(), scanner.number()
            if relative:
                x, y = x + position[0], y + position[1]
            if last_quad is None:
                qx, qy = position
            else:
                qx, qy = 2 * position[0] - last_quad[0], 2 * position[1] - last_quad[1]
            ensure_start()
            flatten_quadratic(position, (qx, qy), (x, y), points)
            last_quad = (qx, qy)
            last_cubic = None
            position = (x, y)
        elif kind == "A":
            rx, ry = scanner.number(), scanner.number()
            angle = scanner.number()
            large_arc = scanner.flag()
            sweep = scanner.flag()
            x, y = scanner.number(), scanner.number()
            if relative:
                x, y = x + position[0], y + position[1]
            segments = arc_to_cubics(position, rx, ry, angle, large_arc, sweep, (x, y))
            ensure_start()
            if segments is None:
                points.append((x, y))
            else:
                cursor = position
                for control1, control2, end in segments:
                    flatten_cubic(cursor, control1, control2, end, points)
                    cursor = end
            last_cubic = last_quad = None
            position = (x, y)
        elif kind == "Z":
            flush(closed=True)
            position = start
            last_cubic = last_quad = None
            command = None
        else:
            raise IconError(f"Unsupported path command {command!r}")

    flush()
    return subpaths


# ---------------------------------------------------------------------------
# Basic shapes -> polylines
# ---------------------------------------------------------------------------
def ellipse_points(cx: float, cy: float, rx: float, ry: float) -> list[tuple[float, float]]:
    steps = int(clamp(round(max(rx, ry) * 0.5), 24, 192))
    return [(cx + rx * math.cos(2 * math.pi * index / steps),
             cy + ry * math.sin(2 * math.pi * index / steps)) for index in range(steps)]


def rect_points(x: float, y: float, width: float, height: float,
                rx: float, ry: float) -> list[tuple[float, float]]:
    rx = clamp(rx, 0.0, width / 2.0)
    ry = clamp(ry, 0.0, height / 2.0)
    if rx <= 0 or ry <= 0:
        return [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]

    points: list[tuple[float, float]] = []
    corners = [
        (x + width - rx, y + ry, -math.pi / 2, 0.0),
        (x + width - rx, y + height - ry, 0.0, math.pi / 2),
        (x + rx, y + height - ry, math.pi / 2, math.pi),
        (x + rx, y + ry, math.pi, 3 * math.pi / 2),
    ]
    for cx, cy, start, end in corners:
        for step in range(9):
            angle = start + (end - start) * step / 8
            points.append((cx + rx * math.cos(angle), cy + ry * math.sin(angle)))
    return points


def points_bbox(points) -> tuple[float, float, float, float]:
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return min(xs), min(ys), max(xs), max(ys)


def subpaths_bbox(subpaths: list[Subpath]) -> tuple[float, float, float, float]:
    points = [point for subpath in subpaths for point in subpath.points]
    return points_bbox(points)


def geometry_for(tag: str, attrs: dict, viewport: tuple[float, float]) -> list[Subpath]:
    view_width, view_height = viewport

    if tag == "path":
        return parse_path_data(attrs.get("d"))

    if tag == "rect":
        x = parse_length(attrs.get("x"), view_width)
        y = parse_length(attrs.get("y"), view_height)
        width = parse_length(attrs.get("width"), view_width)
        height = parse_length(attrs.get("height"), view_height)
        if width <= 0 or height <= 0:
            return []
        radius_x = attrs.get("rx")
        radius_y = attrs.get("ry")
        if radius_x is None and radius_y is None:
            rx = ry = 0.0
        elif radius_x is None:
            rx = ry = parse_length(radius_y, min(view_width, view_height))
        elif radius_y is None:
            rx = ry = parse_length(radius_x, min(view_width, view_height))
        else:
            rx = parse_length(radius_x, min(view_width, view_height))
            ry = parse_length(radius_y, min(view_width, view_height))
        return [Subpath(rect_points(x, y, width, height, rx, ry), True)]

    if tag in ("circle", "ellipse"):
        cx = parse_length(attrs.get("cx"), view_width)
        cy = parse_length(attrs.get("cy"), view_height)
        if tag == "circle":
            radius = parse_length(attrs.get("r"), min(view_width, view_height))
            rx = ry = radius
        else:
            rx = parse_length(attrs.get("rx"), view_width)
            ry = parse_length(attrs.get("ry"), view_height)
        if rx <= 0 or ry <= 0:
            return []
        return [Subpath(ellipse_points(cx, cy, rx, ry), True)]

    if tag == "line":
        return [Subpath([
            (parse_length(attrs.get("x1"), view_width), parse_length(attrs.get("y1"), view_height)),
            (parse_length(attrs.get("x2"), view_width), parse_length(attrs.get("y2"), view_height)),
        ], False)]

    if tag in ("polyline", "polygon"):
        numbers = [float(match.group(0)) for match in NUMBER_RE.finditer(attrs.get("points", ""))]
        points = list(zip(numbers[0::2], numbers[1::2]))
        if len(points) < 2:
            return []
        return [Subpath(points, tag == "polygon")]

    return []


# ---------------------------------------------------------------------------
# Parsed document model
# ---------------------------------------------------------------------------
@dataclass
class GradientStop:
    offset: float
    colour: RGBA


@dataclass
class GradientDef:
    kind: str
    element: ET.Element
    units: str | None
    transform: Matrix | None
    coords: dict[str, str | None]
    spread_method: str | None = None
    stops: list[GradientStop] = field(default_factory=list)
    href: str | None = None


@dataclass
class GradientPaint:
    kind: str
    stops: list[GradientStop]
    x1: float = 0.0
    y1: float = 0.0
    x2: float = 0.0
    y2: float = 0.0
    cx: float = 0.0
    cy: float = 0.0
    radius: float = 0.0


@dataclass
class Shape:
    tag: str
    geom_attrs: dict[str, str]
    matrix: Matrix
    subpaths: list[Subpath]
    geom_bbox: tuple[float, float, float, float]
    fill: str | None
    fill_rgba: RGBA | None
    fill_gradient: GradientPaint | None
    fill_gradient_id: str | None
    fill_rule: str
    fill_opacity: float
    stroke: str | None
    stroke_rgba: RGBA | None
    stroke_gradient: GradientPaint | None
    stroke_gradient_id: str | None
    stroke_width: float
    stroke_opacity: float
    linecap: str
    linejoin: str
    miterlimit: float
    dasharray: tuple[float, ...] | None
    dashoffset: float
    alpha: float

    @property
    def has_fill(self) -> bool:
        return self.fill is not None

    @property
    def has_stroke(self) -> bool:
        return self.stroke is not None and self.stroke_width > 0.0

    @property
    def local_scale(self) -> float:
        return mat_scale(self.matrix)

    @property
    def visible(self) -> bool:
        return self.alpha > 0.001 and (self.has_fill or self.has_stroke)

    def effective_bbox(self) -> tuple[float, float, float, float]:
        x0, y0, x1, y1 = self.geom_bbox
        if not self.has_stroke:
            return self.geom_bbox
        pad = self.stroke_width * self.local_scale / 2.0
        if self.linejoin == "miter":
            pad *= 2.0
        return x0 - pad, y0 - pad, x1 + pad, y1 + pad


@dataclass
class ParsedIcon:
    shapes: list[Shape]
    defs: list[ET.Element]
    viewbox: tuple[float, float, float, float]
    background: str | None
    warnings: list[str]
    unsupported: set[str]
    source_name: str = ""

    @property
    def glyph_bbox(self):
        boxes = [shape.effective_bbox() for shape in self.shapes if shape.visible]
        if not boxes:
            raise IconError("The SVG has no visible shapes")
        return (min(box[0] for box in boxes), min(box[1] for box in boxes),
                max(box[2] for box in boxes), max(box[3] for box in boxes))

    @property
    def paints(self) -> list[str]:
        colours = []
        for shape in self.shapes:
            if not shape.visible:
                continue
            if shape.fill_rgba is not None:
                colours.append(shape.fill_rgba.hex)
            if shape.stroke_rgba is not None:
                colours.append(shape.stroke_rgba.hex)
        return sorted(set(colours))

    @property
    def uses_gradient(self) -> bool:
        return any(shape.fill_gradient or shape.stroke_gradient for shape in self.shapes)


PRESENTATION_ATTRS = (
    "fill", "fill-rule", "fill-opacity", "stroke", "stroke-width", "stroke-opacity",
    "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "stroke-dasharray",
    "stroke-dashoffset", "color", "display", "visibility", "opacity",
)

DEFAULT_STYLE = {
    "fill": "black",
    "fill-rule": "nonzero",
    "fill-opacity": "1",
    "stroke": "none",
    "stroke-width": "1",
    "stroke-opacity": "1",
    "stroke-linecap": "butt",
    "stroke-linejoin": "miter",
    "stroke-miterlimit": "4",
    "stroke-dasharray": "none",
    "stroke-dashoffset": "0",
    "color": "black",
}

DRAWABLE_TAGS = {"path", "rect", "circle", "ellipse", "line", "polyline", "polygon"}
GRADIENT_TAGS = {"linearGradient", "radialGradient"}
SKIP_TAGS = {
    "defs", "style", "title", "desc", "metadata", "stop", "symbol", "marker",
    "clipPath", "mask", "pattern", "filter", "linearGradient", "radialGradient",
    "script", "font", "font-face", "view",
}


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def parse_declarations(text: str | None) -> dict[str, str]:
    declarations: dict[str, str] = {}
    if not text:
        return declarations
    for part in text.split(";"):
        if ":" not in part:
            continue
        key, _, value = part.partition(":")
        key = key.strip().lower()
        value = value.replace("!important", "").strip()
        if key and value:
            declarations[key] = value
    return declarations


@dataclass
class CssRule:
    selector: str
    parts: list[tuple[str, list[str], list[str]]]
    combinators: list[str]
    specificity: tuple[int, int, int]
    order: int
    declarations: dict[str, str]


def _parse_compound(selector: str):
    selector = selector.strip()
    if not selector or "[" in selector or ":" in selector:
        return None
    match = re.match(r"^([a-zA-Z][\w-]*|\*)?((?:[.#][\w-]+)*)$", selector)
    if match is None:
        return None
    tag = match.group(1) or ""
    rest = match.group(2) or ""
    classes = re.findall(r"\.([\w-]+)", rest)
    ids = re.findall(r"#([\w-]+)", rest)
    return tag if tag != "*" else "", classes, ids


def _parse_selector(selector: str):
    """Parse tag/class/id compounds with descendant and child combinators."""
    selector = selector.strip()
    if not selector or any(char in selector for char in "[]:+~"):
        return None
    tokens = re.findall(r"[^\s>]+|>", selector)
    if not tokens or tokens[-1] == ">":
        return None
    parts = []
    combinators = []
    pending = " "
    for token in tokens:
        if token == ">":
            if not parts:
                return None
            pending = ">"
            continue
        compound = _parse_compound(token)
        if compound is None:
            return None
        if parts:
            combinators.append(pending)
        parts.append(compound)
        pending = " "
    return (parts, combinators) if parts else None


def _selector_specificity(parts) -> tuple[int, int, int]:
    ids = sum(len(part[2]) for part in parts)
    classes = sum(len(part[1]) for part in parts)
    tags = sum(1 for part in parts if part[0])
    return ids, classes, tags


def parse_css(text: str, start_order: int = 0) -> list[CssRule]:
    rules: list[CssRule] = []
    text = re.sub(r"/\*.*?\*/", "", text or "", flags=re.S)
    order = start_order
    for block in re.finditer(r"([^{}]+)\{([^{}]*)\}", text):
        declarations = parse_declarations(block.group(2))
        if not declarations:
            continue
        for selector in block.group(1).split(","):
            parsed = _parse_selector(selector)
            if parsed is None:
                continue
            parts, combinators = parsed
            rules.append(CssRule(selector.strip(), parts, combinators,
                                 _selector_specificity(parts), order, declarations))
            order += 1
    return rules


class SvgParser:
    def __init__(self, text: str, source_name: str = "") -> None:
        self.text = text
        self.source_name = source_name
        self.warnings: list[str] = []
        self.unsupported: set[str] = set()
        self.ids: dict[str, ET.Element] = {}
        self.css: list[CssRule] = []
        self.gradients: dict[str, GradientDef] = {}
        self.shapes: list[Shape] = []
        self.defs: list[ET.Element] = []
        self.viewbox: tuple[float, float, float, float] | None = None
        self.root: ET.Element | None = None

    # ---- entry point ----
    def parse(self, strip_background: bool = True) -> ParsedIcon:
        try:
            root = ET.fromstring(self.text)
        except ET.ParseError as error:
            raise IconError(f"Not a valid SVG: {error}") from error

        if local_name(root.tag) != "svg":
            raise IconError("The document's root element is not <svg>")

        self.root = root
        for element in root.iter():
            identifier = element.get("id")
            if identifier:
                self.ids[identifier] = element
            if local_name(element.tag) == "style":
                self.css.extend(parse_css("".join(element.itertext()), len(self.css)))
            if local_name(element.tag) in GRADIENT_TAGS:
                self._register_gradient(element)

        self.viewbox = self._read_viewbox(root)
        viewport = (self.viewbox[2], self.viewbox[3]) if self.viewbox else (CANVAS, CANVAS)

        self._walk(root, IDENTITY, dict(DEFAULT_STYLE), 1.0, 0, False, viewport)

        if not self.shapes:
            raise IconError("The SVG contains no drawable shapes")

        if self.viewbox is None:
            box = self._union_geometry_bbox()
            self.viewbox = (box[0], box[1], max(box[2] - box[0], 1e-6),
                            max(box[3] - box[1], 1e-6))

        self._resolve_gradients()

        background, remaining = self._split_background()
        if strip_background:
            self.shapes = remaining
        if not self.shapes:
            raise IconError("The SVG only contains a background rectangle")

        self.defs = self._referenced_defs()
        return ParsedIcon(
            shapes=self.shapes,
            defs=self.defs,
            viewbox=self.viewbox,
            background=background,
            warnings=self.warnings,
            unsupported=self.unsupported,
            source_name=self.source_name,
        )

    # ---- helpers ----
    def _read_viewbox(self, root: ET.Element):
        raw = root.get("viewBox")
        if raw:
            values = [float(match.group(0)) for match in NUMBER_RE.finditer(raw)]
            if len(values) == 4 and values[2] > 0 and values[3] > 0:
                return values[0], values[1], values[2], values[3]
        width = parse_length(root.get("width"), 0.0)
        height = parse_length(root.get("height"), 0.0)
        if width > 0 and height > 0:
            return 0.0, 0.0, width, height
        return None

    def _union_geometry_bbox(self):
        boxes = [shape.geom_bbox for shape in self.shapes]
        return (min(box[0] for box in boxes), min(box[1] for box in boxes),
                max(box[2] for box in boxes), max(box[3] for box in boxes))

    def _warn(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)

    def _register_gradient(self, element: ET.Element) -> None:
        kind = local_name(element.tag)
        identifier = element.get("id")
        if not identifier:
            return
        stops = []
        for stop in element:
            if local_name(stop.tag) != "stop":
                continue
            style = parse_declarations(stop.get("style"))
            offset = parse_length(stop.get("offset", style.get("offset", "0")), 1.0)
            if stop.get("offset", "").endswith("%") or style.get("offset", "").endswith("%"):
                offset = parse_length(stop.get("offset", style.get("offset", "0")), 1.0)
            colour = parse_color(style.get("stop-color") or stop.get("stop-color") or "black")
            opacity = clamp(as_float(style.get("stop-opacity") or stop.get("stop-opacity") or 1.0, 1.0), 0.0, 1.0)
            if colour is not None:
                stops.append(GradientStop(clamp(offset, 0.0, 1.0), colour.with_alpha(round(opacity * 255))))
        href = element.get("href") or element.get(f"{{{XLINK_NS}}}href")
        transform_text = element.get("gradientTransform")
        self.gradients[identifier] = GradientDef(
            kind=kind,
            element=element,
            units=element.get("gradientUnits"),
            transform=parse_transform(transform_text) if transform_text else None,
            coords={key: element.get(key) for key in ("x1", "y1", "x2", "y2", "cx", "cy", "r", "fx", "fy")},
            spread_method=element.get("spreadMethod"),
            stops=stops,
            href=href[1:] if href and href.startswith("#") else None,
        )

    def _resolved_gradient(self, identifier: str, seen: set[str] | None = None) -> dict:
        """Resolve href inheritance for coords, units, transform and stops."""
        definition = self.gradients.get(identifier)
        if definition is None:
            raise IconError(f"Missing gradient {identifier!r}")
        seen = seen or set()
        if identifier in seen:
            raise IconError(f"Gradient href cycle at {identifier!r}")
        seen.add(identifier)
        parent = self._resolved_gradient(definition.href, seen) if definition.href else {}
        units = definition.units or parent.get("units") or "objectBoundingBox"
        transform = (definition.transform if definition.transform is not None
                     else parent.get("transform", IDENTITY))
        coords = dict(parent.get("coords", {}))
        for key, value in definition.coords.items():
            if value is not None:
                coords[key] = value
        return {
            "kind": definition.kind,
            "units": units,
            "transform": transform,
            "coords": coords,
            "spread_method": definition.spread_method or parent.get("spread_method") or "pad",
            "stops": definition.stops or parent.get("stops", []),
            "element": definition.element,
        }

    @staticmethod
    def _compound_matches(element: ET.Element, compound) -> bool:
        selector_tag, selector_classes, selector_ids = compound
        if selector_tag and selector_tag != local_name(element.tag):
            return False
        if selector_classes:
            classes = set((element.get("class") or "").split())
            if not set(selector_classes).issubset(classes):
                return False
        if selector_ids and element.get("id") not in selector_ids:
            return False
        return True

    def _matches_selector(self, rule: CssRule, chain: tuple[ET.Element, ...]) -> bool:
        parts = rule.parts
        combinators = rule.combinators

        def match(index: int, position: int) -> bool:
            if not self._compound_matches(chain[position], parts[index]):
                return False
            if index == 0:
                return True
            if combinators[index - 1] == ">":
                return position > 0 and match(index - 1, position - 1)
            # Descendant combinator: try every earlier ancestor, not just the
            # nearest one, because an earlier candidate may satisfy the rest.
            for candidate in range(position - 1, -1, -1):
                if match(index - 1, candidate):
                    return True
            return False

        return match(len(parts) - 1, len(chain) - 1)

    def _matching_css(self, element: ET.Element, ancestors: tuple[ET.Element, ...]) -> list[CssRule]:
        chain = ancestors + (element,)
        matches = [rule for rule in self.css if self._matches_selector(rule, chain)]
        matches.sort(key=lambda rule: (rule.specificity, rule.order))
        return matches

    # ---- tree walk ----
    def _walk(self, element, matrix, style, alpha, depth, in_use, viewport,
              ancestors: tuple[ET.Element, ...] = ()):
        if depth > 32:
            self._warn("The SVG is nested too deeply; some shapes were skipped")
            return
        tag = local_name(element.tag)

        if tag in ("title", "desc", "metadata", "script"):
            return
        if tag in SKIP_TAGS:
            if tag == "symbol" and in_use:
                for child in element:
                    self._walk(child, matrix, style, alpha, depth + 1, False, viewport,
                               ancestors + (element,))
            return
        if tag in ("text", "tspan", "textPath", "image", "foreignObject"):
            self.unsupported.add(tag)
            return

        own_declarations: dict[str, str] = {}
        for name in PRESENTATION_ATTRS:
            value = element.get(name)
            if value is not None:
                own_declarations[name] = value
        for rule in self._matching_css(element, ancestors):
            own_declarations.update(rule.declarations)
        own_declarations.update(parse_declarations(element.get("style")))

        current = {key: value for key, value in own_declarations.items()
                   if key not in ("opacity", "display", "visibility")}
        style = {**style, **current}

        if own_declarations.get("display", "").strip().lower() == "none":
            return
        if own_declarations.get("visibility", "").strip().lower() in ("hidden", "collapse"):
            return

        opacity = clamp(as_float(own_declarations.get("opacity", 1.0), 1.0), 0.0, 1.0)
        alpha *= opacity

        transform = mat_mul(matrix, parse_transform(element.get("transform")))

        if tag in ("g", "a", "switch"):
            for child in element:
                self._walk(child, transform, style, alpha, depth + 1, False, viewport,
                           ancestors + (element,))
            return

        if tag == "svg":
            if element is self.root:
                for child in element:
                    self._walk(child, transform, style, alpha, depth + 1, False, viewport,
                               ancestors + (element,))
                return
            nested = self._read_viewbox(element)
            x = parse_length(element.get("x"), viewport[0])
            y = parse_length(element.get("y"), viewport[1])
            width = parse_length(element.get("width"), viewport[0]) or viewport[0]
            height = parse_length(element.get("height"), viewport[1]) or viewport[1]
            if nested:
                scale_x = width / nested[2] if width > 0 else 1.0
                scale_y = height / nested[3] if height > 0 else 1.0
                transform = mat_mul(transform, (scale_x, 0.0, 0.0, scale_y,
                                                x - nested[0] * scale_x,
                                                y - nested[1] * scale_y))
            elif x or y:
                transform = mat_mul(transform, (1.0, 0.0, 0.0, 1.0, x, y))
            for child in element:
                self._walk(child, transform, style, alpha, depth + 1, False,
                           (nested[2], nested[3]) if nested else viewport,
                           ancestors + (element,))
            return

        if tag == "use":
            href = element.get("href") or element.get(f"{{{XLINK_NS}}}href")
            if not href or not href.startswith("#"):
                self._warn("<use> without a local reference was skipped")
                return
            referenced = self.ids.get(href[1:])
            if referenced is None:
                self._warn(f"<use> refers to missing element {href!r}")
                return
            x = parse_length(element.get("x"), viewport[0])
            y = parse_length(element.get("y"), viewport[1])
            transform = mat_mul(transform, (1.0, 0.0, 0.0, 1.0, x, y))
            self._walk(referenced, transform, style, alpha, depth + 1, True, viewport,
                       ancestors + (element,))
            return

        if tag in DRAWABLE_TAGS:
            self._add_shape(element, tag, transform, style, alpha, viewport)
            return

        # Unknown container: walk it so wrapper elements don't eat the glyph.
        if len(element):
            self._warn(f"Unknown element <{tag}> was treated as a group")
            for child in element:
                self._walk(child, transform, style, alpha, depth + 1, False, viewport,
                           ancestors + (element,))
        else:
            self.unsupported.add(tag)

    def _add_shape(self, element, tag, matrix, style, alpha, viewport) -> None:
        for attribute in ("filter", "mask", "clip-path"):
            if element.get(attribute):
                self._warn(f"{attribute} is not supported by the launcher and was ignored")
                break

        subpaths = geometry_for(tag, element.attrib, viewport)
        if not subpaths:
            return
        # Flatten the accumulated transform into the points. The output element
        # keeps the transform + local geometry, but measurements and previews
        # need everything in the root coordinate space.
        if not mat_is_identity(matrix):
            subpaths = [
                Subpath([mat_apply(matrix, x, y) for x, y in subpath.points], subpath.closed)
                for subpath in subpaths
            ]
        geom_bbox = subpaths_bbox(subpaths)
        if geom_bbox[2] - geom_bbox[0] < 1e-9 and geom_bbox[3] - geom_bbox[1] < 1e-9:
            if tag not in ("line", "polyline", "path"):
                return

        fill, fill_rgba, fill_gradient_id, fill_opacity = self._resolve_paint(
            style.get("fill", "black"), style, viewport, "fill-opacity")
        stroke, stroke_rgba, stroke_gradient_id, stroke_opacity = self._resolve_paint(
            style.get("stroke", "none"), style, viewport, "stroke-opacity")
        if tag == "line":
            fill = None
            fill_rgba = None
            fill_gradient_id = None

        stroke_width = parse_length(style.get("stroke-width", "1"), max(viewport))
        if stroke is None:
            stroke_width = 0.0

        dasharray_text = (style.get("stroke-dasharray") or "none").strip().lower()
        dasharray = None
        if dasharray_text not in ("", "none"):
            values = [parse_length(value, max(viewport)) for value in re.split(r"[,\s]+", dasharray_text) if value]
            if values:
                dasharray = tuple(values)

        shape = Shape(
            tag=tag,
            geom_attrs=self._geometry_attrs(element, tag, viewport),
            matrix=matrix,
            subpaths=subpaths,
            geom_bbox=geom_bbox,
            fill=fill,
            fill_rgba=fill_rgba,
            fill_gradient=None,
            fill_gradient_id=fill_gradient_id,
            fill_rule=(style.get("fill-rule") or "nonzero").strip().lower(),
            fill_opacity=fill_opacity,
            stroke=stroke,
            stroke_rgba=stroke_rgba,
            stroke_gradient=None,
            stroke_gradient_id=stroke_gradient_id,
            stroke_width=stroke_width,
            stroke_opacity=stroke_opacity,
            linecap=(style.get("stroke-linecap") or "butt").strip().lower(),
            linejoin=(style.get("stroke-linejoin") or "miter").strip().lower(),
            miterlimit=as_float(style.get("stroke-miterlimit"), 4.0),
            dasharray=dasharray,
            dashoffset=parse_length(style.get("stroke-dashoffset"), max(viewport)),
            alpha=alpha,
        )
        self.shapes.append(shape)

    def _resolve_paint(self, value, style, viewport, opacity_key):
        raw = (value or "").strip()
        if not raw or raw.lower() in ("none", "transparent"):
            return None, None, None, 1.0
        if raw.lower() == "currentcolor":
            raw = style.get("color", "black")
        match = re.match(r"url\(\s*['\"]?#([^)'\"]+)['\"]?\s*\)", raw)
        if match:
            identifier = match.group(1)
            try:
                resolved = self._resolved_gradient(identifier)
            except IconError as error:
                self._warn(f"Gradient {identifier!r} was replaced with black: {error}")
                return "#000000", RGBA(0, 0, 0), None, 1.0
            if not resolved["stops"]:
                self._warn(f"Gradient {identifier!r} has no stops; using black")
                return "#000000", RGBA(0, 0, 0), None, 1.0
            opacity = clamp(as_float(style.get(opacity_key, "1"), 1.0), 0.0, 1.0)
            return f"url(#{identifier})", None, identifier, opacity

        colour = parse_color(raw, style.get("color", "black"))
        if colour is None:
            self._warn(f"Unsupported paint {raw!r} was replaced with black")
            return "#000000", RGBA(0, 0, 0), None, 1.0
        opacity = clamp(as_float(style.get(opacity_key, "1"), 1.0), 0.0, 1.0) * (colour.a / 255.0)
        if opacity <= 0.0:
            return None, None, None, 0.0
        return colour.hex, colour.with_alpha(255), None, opacity

    def _geometry_attrs(self, element, tag, viewport):
        attrs: dict[str, str] = {}
        width, height = viewport
        if tag == "path":
            attrs["d"] = element.get("d", "")
        elif tag == "rect":
            attrs["x"] = fmt(parse_length(element.get("x"), width))
            attrs["y"] = fmt(parse_length(element.get("y"), height))
            attrs["width"] = fmt(parse_length(element.get("width"), width))
            attrs["height"] = fmt(parse_length(element.get("height"), height))
            if element.get("rx") is not None or element.get("ry") is not None:
                radius_x = element.get("rx")
                radius_y = element.get("ry")
                if radius_x is None:
                    radius_x = radius_y
                if radius_y is None:
                    radius_y = radius_x
                attrs["rx"] = fmt(parse_length(radius_x, min(width, height)))
                attrs["ry"] = fmt(parse_length(radius_y, min(width, height)))
        elif tag == "circle":
            attrs["cx"] = fmt(parse_length(element.get("cx"), width))
            attrs["cy"] = fmt(parse_length(element.get("cy"), height))
            attrs["r"] = fmt(parse_length(element.get("r"), min(width, height)))
        elif tag == "ellipse":
            attrs["cx"] = fmt(parse_length(element.get("cx"), width))
            attrs["cy"] = fmt(parse_length(element.get("cy"), height))
            attrs["rx"] = fmt(parse_length(element.get("rx"), width))
            attrs["ry"] = fmt(parse_length(element.get("ry"), height))
        elif tag == "line":
            attrs["x1"] = fmt(parse_length(element.get("x1"), width))
            attrs["y1"] = fmt(parse_length(element.get("y1"), height))
            attrs["x2"] = fmt(parse_length(element.get("x2"), width))
            attrs["y2"] = fmt(parse_length(element.get("y2"), height))
        elif tag in ("polyline", "polygon"):
            attrs["points"] = element.get("points", "").strip()
        return attrs

    def _resolve_gradients(self) -> None:
        for shape in self.shapes:
            if shape.fill_gradient_id:
                shape.fill_gradient = self._build_gradient(shape.fill_gradient_id, shape)
            if shape.stroke_gradient_id:
                shape.stroke_gradient = self._build_gradient(shape.stroke_gradient_id, shape)

    def _build_gradient(self, identifier: str, shape: Shape) -> GradientPaint:
        try:
            resolved = self._resolved_gradient(identifier)
        except IconError:
            definition = self.gradients[identifier]
            resolved = {"kind": definition.kind, "units": "objectBoundingBox",
                        "transform": IDENTITY, "coords": {}, "stops": []}
        stops = resolved["stops"]
        kind = resolved["kind"]
        units = resolved["units"]
        transform = resolved["transform"]
        coords = resolved["coords"]
        if units == "objectBoundingBox":
            x0, y0, x1, y1 = shape.geom_bbox
            base: Matrix = (x1 - x0, 0.0, 0.0, y1 - y0, x0, y0)
        else:
            base = shape.matrix
        matrix = mat_mul(base, transform)

        def coordinate(key, default):
            raw = coords.get(key)
            if raw is None:
                raw = default
            if units == "objectBoundingBox":
                return as_float(str(raw).rstrip("%"), 0.0) / (100.0 if str(raw).endswith("%") else 1.0)
            return parse_length(raw, max(shape.geom_bbox[2] - shape.geom_bbox[0],
                                        shape.geom_bbox[3] - shape.geom_bbox[1], 1.0))

        if kind == "linearGradient":
            p1 = mat_apply(matrix, coordinate("x1", "0%"), coordinate("y1", "0%"))
            p2 = mat_apply(matrix, coordinate("x2", "100%"), coordinate("y2", "0%"))
            return GradientPaint("linear", stops, x1=p1[0], y1=p1[1], x2=p2[0], y2=p2[1])

        cx, cy = mat_apply(matrix, coordinate("cx", "50%"), coordinate("cy", "50%"))
        radius = coordinate("r", "50%")
        origin = mat_apply(matrix, 0.0, 0.0)
        unit_x = mat_apply(matrix, 1.0, 0.0)
        unit_y = mat_apply(matrix, 0.0, 1.0)
        average = (math.dist(origin, unit_x) + math.dist(origin, unit_y)) / 2.0
        return GradientPaint("radial", stops, cx=cx, cy=cy, radius=radius * average)

    def _split_background(self):
        assert self.viewbox is not None
        viewport_x, viewport_y, viewport_w, viewport_h = self.viewbox
        tolerance = 0.03
        for index, shape in enumerate(self.shapes):
            if shape.tag != "rect" or shape.fill is None or shape.has_stroke:
                continue
            x0, y0, x1, y1 = shape.geom_bbox
            if not (x0 <= viewport_x + viewport_w * tolerance
                    and y0 <= viewport_y + viewport_h * tolerance
                    and x1 >= viewport_x + viewport_w * (1 - tolerance)
                    and y1 >= viewport_y + viewport_h * (1 - tolerance)):
                continue
            if shape.fill_rgba is None:
                self._warn("A full-size rectangle with a gradient fill was kept in the glyph")
                continue
            return shape.fill_rgba.hex, self.shapes[:index] + self.shapes[index + 1:]
        return None, self.shapes

    def _referenced_defs(self) -> list[ET.Element]:
        used = set()
        for shape in self.shapes:
            for paint in (shape.fill, shape.stroke):
                match = re.match(r"url\(#([^)]+)\)", paint or "")
                if match:
                    used.add(match.group(1))
        if not used:
            return []
        # Follow href chains so inherited stops are available.
        pending = list(used)
        while pending:
            identifier = pending.pop()
            definition = self.gradients.get(identifier)
            if definition is not None and definition.href and definition.href not in used:
                used.add(definition.href)
                pending.append(definition.href)

        definitions = []
        for identifier, definition in self.gradients.items():
            if identifier not in used:
                continue
            try:
                resolved = self._resolved_gradient(identifier)
            except IconError:
                continue
            clone = _clean_definition(definition.element)
            clone.attrib.pop("href", None)
            # Resolve inherited coordinates, units and transform before the
            # href is removed, so the device renderer doesn't need href support.
            clone.set("gradientUnits", resolved["units"])
            transform = resolved["transform"]
            if mat_is_identity(transform):
                clone.attrib.pop("gradientTransform", None)
            else:
                clone.set("gradientTransform", _matrix_attr(transform))
            if resolved["spread_method"] != "pad":
                clone.set("spreadMethod", resolved["spread_method"])
            coords = resolved["coords"]
            if resolved["kind"] == "linearGradient":
                clone.set("x1", coords.get("x1", "0%"))
                clone.set("y1", coords.get("y1", "0%"))
                clone.set("x2", coords.get("x2", "100%"))
                clone.set("y2", coords.get("y2", "0%"))
            else:
                cx = coords.get("cx", "50%")
                cy = coords.get("cy", "50%")
                clone.set("cx", cx)
                clone.set("cy", cy)
                clone.set("r", coords.get("r", "50%"))
                clone.set("fx", coords.get("fx", cx))
                clone.set("fy", coords.get("fy", cy))
            if not any(local_name(child.tag) == "stop" for child in clone):
                for stop in resolved["stops"]:
                    ET.SubElement(clone, "stop", {
                        "offset": fmt(stop.offset),
                        "stop-color": stop.colour.hex,
                        "stop-opacity": fmt(stop.colour.a / 255.0),
                    })
            definitions.append(clone)
        return definitions


def _clean_definition(element: ET.Element) -> ET.Element:
    tag = local_name(element.tag)
    clone = ET.Element(tag)
    attributes = {local_name(key): value for key, value in element.attrib.items()}
    if tag == "stop":
        # Inline style overrides presentation attributes, and the device
        # renderer should not need a CSS parser for a gradient stop.
        attributes.pop("style", None)
        attributes.update(parse_declarations(element.get("style")))
        colour = parse_color(attributes.get("stop-color"))
        if colour is not None:
            attributes["stop-color"] = colour.hex
    for key, value in attributes.items():
        clone.set(key, value)
    for child in element:
        clone.append(_clean_definition(child))
    return clone


def wrap_svg_fragment(text: str) -> str:
    """Turn pasted path data or a `<path>` fragment into a full SVG document."""
    stripped = text.strip()
    if re.search(r"<\s*svg\b", stripped, re.I):
        return stripped
    if re.search(r"<\s*(path|g|rect|circle|ellipse|polygon|polyline)\b", stripped, re.I):
        return (f'<svg xmlns="{SVG_NS}" viewBox="0 -960 960 960">{stripped}</svg>')
    # Bare path data, possibly with a d="..." prefix.
    match = re.search(r"\bd\s*=\s*[\"']([^\"']+)[\"']", stripped)
    data = match.group(1) if match else stripped
    data = data.strip().strip("\"'")
    if not data:
        raise IconError("Paste an SVG document or path data first")
    return f'<svg xmlns="{SVG_NS}" viewBox="0 -960 960 960"><path d="{data}"/></svg>'


def parse_document(text: str, *, strip_background: bool = True,
                   source_name: str = "") -> ParsedIcon:
    text = wrap_svg_fragment(text)
    parser = SvgParser(text, source_name)
    return parser.parse(strip_background=strip_background)


# ---------------------------------------------------------------------------
# Fitting and SVG output
# ---------------------------------------------------------------------------
@dataclass
class IconOptions:
    background: str = DEFAULT_BG
    glyph_mode: str = "auto"
    glyph_colour: str = DEFAULT_GLYPH
    colour_size: float = DEFAULT_COLOUR_SIZE
    mono_size: float = DEFAULT_MONO_SIZE
    min_stroke: float = DEFAULT_MIN_STROKE
    keep_layout: bool = False


@dataclass
class FitInfo:
    scale: float
    offset_x: float
    offset_y: float
    glyph_width: float
    glyph_height: float
    bbox: tuple[float, float, float, float]


@dataclass
class BuildResult:
    colour_svg: str
    mono_svg: str
    colour_fit: FitInfo
    mono_fit: FitInfo
    warnings: list[str]


def _union_bbox(shapes: list[Shape]):
    boxes = [shape.effective_bbox() for shape in shapes if shape.visible]
    if not boxes:
        raise IconError("The SVG has no visible shapes")
    return (min(box[0] for box in boxes), min(box[1] for box in boxes),
            max(box[2] for box in boxes), max(box[3] for box in boxes))


def _fit_once(shapes: list[Shape], target: float) -> FitInfo:
    x0, y0, x1, y1 = _union_bbox(shapes)
    width = max(x1 - x0, 1e-6)
    height = max(y1 - y0, 1e-6)
    scale = target / max(width, height)
    return FitInfo(
        scale=scale,
        offset_x=(CANVAS - width * scale) / 2.0 - x0 * scale,
        offset_y=(CANVAS - height * scale) / 2.0 - y0 * scale,
        glyph_width=width * scale,
        glyph_height=height * scale,
        bbox=(x0, y0, x1, y1),
    )


def _viewbox_fit(viewbox: tuple[float, float, float, float]) -> FitInfo:
    """Fit the whole source canvas into the target canvas, keeping its layout."""
    x, y, width, height = viewbox
    scale = CANVAS / max(width, height, 1e-6)
    return FitInfo(
        scale=scale,
        offset_x=(CANVAS - width * scale) / 2.0 - x * scale,
        offset_y=(CANVAS - height * scale) / 2.0 - y * scale,
        glyph_width=width * scale,
        glyph_height=height * scale,
        bbox=(x, y, x + width, y + height),
    )


def fit_shapes(shapes: list[Shape], target: float,
               min_stroke: float | None = None) -> FitInfo:
    fit = _fit_once(shapes, target)
    if min_stroke:
        for _ in range(4):
            changed = False
            for shape in shapes:
                if not shape.has_stroke:
                    continue
                effective = shape.stroke_width * shape.local_scale * fit.scale
                if effective + 1e-6 < min_stroke:
                    shape.stroke_width = min_stroke / (shape.local_scale * fit.scale)
                    changed = True
            if not changed:
                break
            fit = _fit_once(shapes, target)
    return _fit_once(shapes, target)


def _matrix_attr(matrix: Matrix) -> str:
    # Two decimals are fine for path coordinates, but the fit scale is often
    # around 0.08; rounding that to 0.08 changes the glyph size noticeably.
    return "matrix(" + " ".join(f"{value:.6g}" for value in matrix) + ")"


def _shape_element(shape: Shape, paint_mode: str, colour: str | None,
                   min_stroke: float | None, fit: FitInfo) -> ET.Element | None:
    if not shape.visible:
        return None

    element = ET.Element(shape.tag)
    for key, value in shape.geom_attrs.items():
        element.set(key, value)
    if not mat_is_identity(shape.matrix):
        element.set("transform", _matrix_attr(shape.matrix))

    if paint_mode == "mono":
        fill = INK if shape.has_fill else None
        stroke = INK if shape.has_stroke else None
    else:
        fill = shape.fill
        stroke = shape.stroke
        if paint_mode == "solid":
            fill = colour if shape.has_fill else None
            stroke = colour if shape.has_stroke else None

    if fill is None:
        element.set("fill", "none")
    elif paint_mode == "mono":
        element.set("fill", INK)
    elif fill.lower() not in ("#000000", "black"):
        element.set("fill", fill)

    if shape.fill_rule == "evenodd":
        element.set("fill-rule", "evenodd")
    if shape.has_fill and paint_mode != "mono" and shape.fill_opacity < 0.999:
        element.set("fill-opacity", fmt(shape.fill_opacity))

    if stroke is not None and shape.has_stroke:
        # The default stroke is `none`, so a black stroke still has to be written.
        element.set("stroke", stroke)

        width = shape.stroke_width
        if paint_mode == "mono" and min_stroke:
            final_width = width * shape.local_scale * fit.scale
            if final_width < min_stroke:
                width = min_stroke / (shape.local_scale * fit.scale)
        if abs(width - 1.0) > 1e-6:
            element.set("stroke-width", fmt(width))

        if paint_mode == "mono":
            element.set("stroke-linecap", "round")
            element.set("stroke-linejoin", "round")
        else:
            if shape.linecap != "butt":
                element.set("stroke-linecap", shape.linecap)
            if shape.linejoin != "miter":
                element.set("stroke-linejoin", shape.linejoin)
            if shape.linejoin == "miter" and abs(shape.miterlimit - 4.0) > 1e-6:
                element.set("stroke-miterlimit", fmt(shape.miterlimit))
        if shape.dasharray:
            element.set("stroke-dasharray", " ".join(fmt(value) for value in shape.dasharray))
            if shape.dashoffset:
                element.set("stroke-dashoffset", fmt(shape.dashoffset))
        if paint_mode != "mono" and shape.stroke_opacity < 0.999:
            element.set("stroke-opacity", fmt(shape.stroke_opacity))

    if paint_mode != "mono" and shape.alpha < 0.999:
        element.set("opacity", fmt(shape.alpha))

    return element


def _serialize(root: ET.Element) -> str:
    return ET.tostring(root, encoding="unicode", short_empty_elements=True).replace(" />", "/>")


def _svg_root(width: int = CANVAS, height: int = CANVAS) -> ET.Element:
    return ET.Element("svg", {
        "xmlns": SVG_NS,
        "width": str(width),
        "height": str(height),
        "viewBox": f"0 0 {CANVAS} {CANVAS}",
    })


def _fit_group(fit: FitInfo) -> ET.Element:
    matrix: Matrix = (fit.scale, 0.0, 0.0, fit.scale, fit.offset_x, fit.offset_y)
    attributes = {"data-fit": "1"}
    if not mat_is_identity(matrix):
        attributes["transform"] = _matrix_attr(matrix)
    return ET.Element("g", attributes)


def build_colour_svg(icon: ParsedIcon, options: IconOptions) -> tuple[str, FitInfo]:
    shapes = copy.deepcopy(icon.shapes)
    if options.keep_layout and icon.background:
        fit = _viewbox_fit(icon.viewbox)
    else:
        fit = fit_shapes(shapes, options.colour_size)

    if options.glyph_mode == "auto":
        colour = auto_glyph_colour(options.background)
    elif options.glyph_mode == "solid":
        colour = normalize_hex(options.glyph_colour, DEFAULT_GLYPH)
    else:
        colour = None

    root = _svg_root()
    ET.SubElement(root, "rect", {
        "width": str(CANVAS), "height": str(CANVAS),
        "fill": normalize_hex(options.background),
    })
    group = _fit_group(fit)
    root.append(group)

    if options.glyph_mode == "source" and icon.defs:
        defs = ET.SubElement(group, "defs")
        for definition in icon.defs:
            defs.append(_clean_definition(definition))

    paint_mode = "source" if options.glyph_mode == "source" else "solid"
    for shape in shapes:
        element = _shape_element(shape, paint_mode, colour, None, fit)
        if element is not None:
            group.append(element)

    return _serialize(root), fit


def build_mono_svg(icon: ParsedIcon, options: IconOptions) -> tuple[str, FitInfo]:
    shapes = copy.deepcopy(icon.shapes)
    fit = fit_shapes(shapes, options.mono_size, min_stroke=options.min_stroke)

    # The mono guide uses 24 px attributes with the 96-unit drawing space.
    root = _svg_root(24, 24)
    group = _fit_group(fit)
    root.append(group)
    for shape in shapes:
        element = _shape_element(shape, "mono", INK, options.min_stroke, fit)
        if element is not None:
            group.append(element)
    return _serialize(root), fit


def _base_warnings(icon: ParsedIcon) -> list[str]:
    warnings = list(icon.warnings)
    if "text" in icon.unsupported or "tspan" in icon.unsupported:
        warnings.append("Text was dropped: fonts are not available on the device")
    if icon.uses_gradient:
        warnings.append("Gradients are kept in the colour icon; the preview approximates them")
    if len(set(icon.paints)) > 1:
        warnings.append("Multi-colour source: the mono icon collapses to one colour")
    for tag in sorted(icon.unsupported):
        if tag in ("text", "tspan", "textPath"):
            continue
        warnings.append(f"<{tag}> is not supported and was skipped")
    return warnings


def _sanity_check(colour_svg: str, mono_svg: str) -> list[str]:
    problems = []
    for label, text, size in (("colour", colour_svg, 96), ("mono", mono_svg, 24)):
        if f'width="{size}"' not in text or f'height="{size}"' not in text:
            problems.append(f"The {label} icon is not {size} px")
        if 'viewBox="0 0 96 96"' not in text:
            problems.append(f"The {label} icon does not use a 96×96 viewBox")
    if "url(#" in mono_svg:
        problems.append("The mono icon still refers to a gradient")
    if 'opacity="' in mono_svg:
        problems.append("The mono icon still has opacity")
    for match in re.finditer(r'(?:fill|stroke)="([^"]+)"', mono_svg):
        paint = match.group(1).strip().lower()
        if paint not in ("none", "#000000", "black"):
            problems.append(f"The mono icon is not pure black ({match.group(1)})")
            break
    if "<text" in mono_svg or "<text" in colour_svg:
        problems.append("An output icon contains <text>")
    return problems


def build_outputs(icon: ParsedIcon, options: IconOptions) -> BuildResult:
    colour_svg, colour_fit = build_colour_svg(icon, options)
    mono_svg, mono_fit = build_mono_svg(icon, options)
    warnings = _base_warnings(icon)

    if options.colour_size > CANVAS:
        warnings.append("The colour glyph is larger than the canvas and will be clipped")
    if options.mono_size > CANVAS:
        warnings.append("The mono glyph is larger than the canvas and will be clipped")

    if icon.background:
        warnings.append(f"Full-size source rectangle used as the background ({icon.background})")

    if options.glyph_mode == "source" and not (options.keep_layout and icon.background):
        background = parse_color(options.background) or RGBA(255, 255, 255)
        worst = None
        for paint in icon.paints:
            parsed = parse_color(paint)
            if parsed is None:
                continue
            ratio = contrast_ratio(parsed, background)
            if worst is None or ratio < worst[1]:
                worst = (paint, ratio)
        if worst is not None and worst[1] < 2.2:
            warnings.append(f"Low contrast: {worst[0]} on {options.background} is {worst[1]:.1f}:1")

    warnings.extend(_sanity_check(colour_svg, mono_svg))
    return BuildResult(colour_svg, mono_svg, colour_fit, mono_fit, warnings)


# ---------------------------------------------------------------------------
# Rasteriser (preview only)
# ---------------------------------------------------------------------------
def _polygon_area(points: list[tuple[float, float]]) -> float:
    if len(points) < 3:
        return 0.0
    return 0.5 * sum(
        points[index][0] * points[(index + 1) % len(points)][1]
        - points[(index + 1) % len(points)][0] * points[index][1]
        for index in range(len(points))
    )


def _point_in_polygon(point: tuple[float, float], points: list[tuple[float, float]]) -> bool:
    x, y = point
    inside = False
    j = len(points) - 1
    for i in range(len(points)):
        xi, yi = points[i]
        xj, yj = points[j]
        if (yi > y) != (yj > y):
            crossing = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < crossing:
                inside = not inside
        j = i
    return inside


def _interior_point(points: list[tuple[float, float]]) -> tuple[float, float]:
    count = len(points)
    average = (sum(point[0] for point in points) / count,
               sum(point[1] for point in points) / count)
    if _point_in_polygon(average, points):
        return average
    # Concave contour: take the middle of the first inside span on the
    # horizontal line through the bounding box centre.
    ys = [point[1] for point in points]
    y = (min(ys) + max(ys)) / 2.0
    crossings = []
    j = count - 1
    for i in range(count):
        xi, yi = points[i]
        xj, yj = points[j]
        if (yi > y) != (yj > y):
            crossings.append((xj - xi) * (y - yi) / (yj - yi) + xi)
        j = i
    crossings.sort()
    for left, right in zip(crossings[0::2], crossings[1::2]):
        if right - left > 1e-6:
            return (left + right) / 2.0, y
    return ((points[0][0] + points[1][0]) / 2.0, (points[0][1] + points[1][1]) / 2.0)


def _polygon_contains(container: list[tuple[float, float]],
                      points: list[tuple[float, float]]) -> bool:
    """True when `points` (a contour) is fully inside `container`."""
    if min(point[0] for point in points) < min(point[0] for point in container) - 1e-6:
        return False
    if max(point[0] for point in points) > max(point[0] for point in container) + 1e-6:
        return False
    if min(point[1] for point in points) < min(point[1] for point in container) - 1e-6:
        return False
    if max(point[1] for point in points) > max(point[1] for point in container) + 1e-6:
        return False

    stride = max(1, len(points) // 12)
    samples = points[::stride]
    samples += [((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
                for a, b in zip(points[::stride], points[1::stride])]
    return all(_point_in_polygon(point, container) for point in samples)


def _paint_mask(size, subpaths: list[Subpath], fill_rule: str):
    """Rasterise a fill, honouring even-odd or true nonzero winding.

    Font outlines (and many icon paths) nest a positive contour inside a
    negative hole inside a positive contour. A simple union/XOR gets that
    wrong, so for nonzero we work out each contour's nesting parent and fill
    only the winding-correct regions. This is exact for the simple,
    non-self-intersecting contours used by icons and fonts.
    """
    if Image is None:
        return None

    paths = [subpath for subpath in subpaths if len(subpath.points) >= 3]
    if not paths:
        return None

    if fill_rule == "evenodd":
        mask = Image.new("L", size, 0)
        for subpath in paths:
            layer = Image.new("L", size, 0)
            ImageDraw.Draw(layer).polygon(subpath.points, fill=255)
            mask = ImageChops.difference(mask, layer)
        return mask

    interiors = []
    signs = []
    for subpath in paths:
        layer = Image.new("L", size, 0)
        ImageDraw.Draw(layer).polygon(subpath.points, fill=255)
        interiors.append(layer)
        signs.append(1 if _polygon_area(subpath.points) >= 0 else -1)

    parents: list[int | None] = [None] * len(paths)
    areas = [abs(_polygon_area(subpath.points)) for subpath in paths]
    for index, subpath in enumerate(paths):
        best = None
        best_area = None
        for other_index, other in enumerate(paths):
            if index == other_index or areas[other_index] <= areas[index]:
                continue
            if _polygon_contains(other.points, subpath.points):
                if best is None or areas[other_index] < best_area:
                    best = other_index
                    best_area = areas[other_index]
        parents[index] = best

    children: list[list[int]] = [[] for _ in paths]
    for index, parent in enumerate(parents):
        if parent is not None:
            children[parent].append(index)

    def winding(index: int) -> int:
        total = 0
        seen = set()
        while index is not None and index not in seen:
            seen.add(index)
            total += signs[index]
            index = parents[index]
        return total

    mask = Image.new("L", size, 0)
    for index in range(len(paths)):
        if winding(index) == 0:
            continue
        region = interiors[index]
        for child in children[index]:
            region = ImageChops.subtract(region, interiors[child])
        mask = ImageChops.lighter(mask, region)
    return mask


def _dash_segments(points, closed, pattern, offset):
    source = list(points)
    if closed and len(source) > 1 and source[0] != source[-1]:
        source.append(source[0])
    if not pattern:
        return [source] if len(source) >= 2 else []
    values = list(pattern)
    if not values:
        return [source]
    if len(values) % 2:
        values *= 2
    period = sum(values)
    if period <= 1e-9:
        return [source]

    # A large (or negative) offset must not walk the pattern forever. Reduce it
    # modulo one period, then skip whole spans without changing their lengths.
    offset %= period
    index = 0
    while offset >= values[index]:
        offset -= values[index]
        index = (index + 1) % len(values)
        if offset <= 1e-9:
            break

    segments = []
    current = [source[0]]
    remaining = values[index] - offset
    on = index % 2 == 0
    for start, end in zip(source, source[1:]):
        segment_length = math.dist(start, end)
        if segment_length <= 1e-9:
            continue
        position = 0.0
        while position < segment_length - 1e-9:
            step = min(remaining, segment_length - position)
            t0 = position / segment_length
            t1 = (position + step) / segment_length
            p0 = (start[0] + (end[0] - start[0]) * t0, start[1] + (end[1] - start[1]) * t0)
            p1 = (start[0] + (end[0] - start[0]) * t1, start[1] + (end[1] - start[1]) * t1)
            if on:
                if not current:
                    current = [p0]
                current.append(p1)
            position += step
            remaining -= step
            if remaining <= 1e-9:
                if on and len(current) >= 2:
                    segments.append(current)
                current = []
                on = not on
                index = (index + 1) % len(values)
                remaining = values[index]
    if on and len(current) >= 2:
        segments.append(current)
    return segments


def _stroke_mask(size, shape: Shape, scale: float, origin=(0.0, 0.0)):
    width = max(1.0, shape.stroke_width * shape.local_scale * scale)
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    radius = width / 2.0
    origin_x, origin_y = origin

    def mapped(points):
        return [((x - origin_x) * scale, (y - origin_y) * scale) for x, y in points]

    for subpath in shape.subpaths:
        segments = _dash_segments(subpath.points, subpath.closed, shape.dasharray, shape.dashoffset)
        for segment in segments:
            if len(segment) < 2:
                continue
            points = mapped(segment)
            draw.line(points, fill=255, width=int(round(width)), joint="curve")
            if shape.linecap == "round" and width > 1:
                for point in (points[0], points[-1]):
                    draw.ellipse((point[0] - radius, point[1] - radius,
                                  point[0] + radius, point[1] + radius), fill=255)
        if subpath.closed and len(subpath.points) > 2:
            point = mapped([subpath.points[0]])[0]
            if shape.linejoin == "round" and width > 1:
                draw.ellipse((point[0] - radius, point[1] - radius,
                              point[0] + radius, point[1] + radius), fill=255)
    return mask


def _gradient_lut(paint: GradientPaint, entries: int = 256):
    stops = sorted(paint.stops, key=lambda stop: stop.offset)
    if not stops:
        stops = [GradientStop(0.0, RGBA(0, 0, 0)), GradientStop(1.0, RGBA(0, 0, 0))]
    lut = []
    for index in range(entries):
        t = index / (entries - 1)
        before = stops[0]
        after = stops[-1]
        for left, right in zip(stops, stops[1:]):
            if left.offset <= t <= right.offset:
                before, after = left, right
                break
        span = after.offset - before.offset
        blend = 0.0 if span <= 1e-9 else (t - before.offset) / span
        lut.append(RGBA(
            round(before.colour.r + (after.colour.r - before.colour.r) * blend),
            round(before.colour.g + (after.colour.g - before.colour.g) * blend),
            round(before.colour.b + (after.colour.b - before.colour.b) * blend),
            round(before.colour.a + (after.colour.a - before.colour.a) * blend),
        ))
    return lut


def _gradient_image(paint: GradientPaint, size, viewbox, scale_x, scale_y, low: int = 96):
    lut = _gradient_lut(paint)
    low_image = Image.new("RGBA", (low, low))
    pixels = low_image.load()
    view_x, view_y, view_w, view_h = viewbox
    dx = paint.x2 - paint.x1
    dy = paint.y2 - paint.y1
    denominator = dx * dx + dy * dy

    for py in range(low):
        world_y = view_y + (py + 0.5) / low * view_h
        for px in range(low):
            world_x = view_x + (px + 0.5) / low * view_w
            if paint.kind == "linear":
                if denominator <= 1e-12:
                    t = 0.0
                else:
                    t = ((world_x - paint.x1) * dx + (world_y - paint.y1) * dy) / denominator
            else:
                t = math.hypot(world_x - paint.cx, world_y - paint.cy) / paint.radius if paint.radius > 1e-9 else 1.0
            colour = lut[int(clamp(t, 0.0, 1.0) * (len(lut) - 1))]
            pixels[px, py] = (colour.r, colour.g, colour.b, colour.a)

    return low_image.resize(size, Image.BILINEAR)


def _composite(canvas, mask, paint: RGBA | GradientPaint | None, paint_rgba: RGBA | None,
               alpha: float, shape: Shape, icon_viewbox, scale_x, scale_y):
    if paint is None and paint_rgba is None:
        return canvas
    if isinstance(paint, GradientPaint):
        layer = _gradient_image(paint, canvas.size, icon_viewbox, scale_x, scale_y)
    else:
        layer = Image.new("RGBA", canvas.size, (paint_rgba or RGBA(0, 0, 0)).tuple)
    if alpha < 0.999:
        mask = mask.point(lambda value: round(value * alpha))
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
    return Image.alpha_composite(canvas, layer)


def render_parsed(icon: ParsedIcon, pixels: int) -> "Image.Image":
    if Image is None:
        raise IconError("Pillow is needed for previews")

    view_x, view_y, view_w, view_h = icon.viewbox
    supersample = SUPERSAMPLE if pixels <= 48 else 2
    size = (max(1, pixels * supersample), max(1, pixels * supersample))
    scale_x = size[0] / view_w
    scale_y = size[1] / view_h

    canvas = Image.new("RGBA", size, (0, 0, 0, 0))

    for shape in icon.shapes:
        if not shape.visible:
            continue
        points = [[((x - view_x) * scale_x, (y - view_y) * scale_y) for x, y in subpath.points]
                  for subpath in shape.subpaths]
        scaled_subpaths = [Subpath(pts, subpath.closed) for pts, subpath in zip(points, shape.subpaths)]

        if shape.has_fill:
            mask = _paint_mask(size, scaled_subpaths, shape.fill_rule)
            if mask is not None:
                if shape.fill_gradient is not None:
                    canvas = _composite(canvas, mask, shape.fill_gradient, None,
                                        shape.alpha * shape.fill_opacity, shape, icon.viewbox,
                                        scale_x, scale_y)
                else:
                    canvas = _composite(canvas, mask, None, shape.fill_rgba,
                                        shape.alpha * shape.fill_opacity, shape, icon.viewbox,
                                        scale_x, scale_y)
        if shape.has_stroke:
            mask = _stroke_mask((size[0], size[1]), shape,
                                scale_x if scale_x == scale_y else (scale_x + scale_y) / 2,
                                origin=(view_x, view_y))
            if shape.stroke_gradient is not None:
                canvas = _composite(canvas, mask, shape.stroke_gradient, None,
                                    shape.alpha * shape.stroke_opacity, shape, icon.viewbox,
                                    scale_x, scale_y)
            else:
                canvas = _composite(canvas, mask, None, shape.stroke_rgba,
                                    shape.alpha * shape.stroke_opacity, shape, icon.viewbox,
                                    scale_x, scale_y)

    if size[0] != pixels or size[1] != pixels:
        canvas = canvas.resize((pixels, pixels), Image.LANCZOS)
    return canvas


def flatten_preview(image, background: str, tint: str | None = None):
    if Image is None:
        return image
    base = Image.new("RGBA", image.size, (parse_color(background) or RGBA(255, 255, 255)).tuple)
    if tint is not None:
        tinted = Image.new("RGBA", image.size, (parse_color(tint) or RGBA(255, 255, 255)).tuple)
        tinted.putalpha(image.getchannel("A"))
        image = tinted
    return Image.alpha_composite(base, image).convert("RGB")


# ---------------------------------------------------------------------------
# Files and app scanning
# ---------------------------------------------------------------------------
def find_repo() -> Path | None:
    here = Path(__file__).resolve()
    for start in (Path.cwd().resolve(), here.parent):
        for path in (start, *start.parents):
            if (path / "Apps").is_dir() and (path / "Scripts").is_dir():
                return path
    return None


def read_properties(path: Path) -> dict[str, str]:
    properties: dict[str, str] = {}
    try:
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                stripped = line.strip()
                if not stripped or stripped.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                properties[key.strip()] = value.strip()
    except OSError:
        pass
    return properties


def scan_apps(repo: Path | None) -> list[tuple[str, str, Path]]:
    apps: list[tuple[str, str, Path]] = []
    if repo is None:
        return apps
    root = repo / "Apps"
    if not root.is_dir():
        return apps
    for path in sorted(root.iterdir(), key=lambda item: item.name.lower()):
        manifest = path / "manifest.properties"
        if not path.is_dir() or not manifest.is_file():
            continue
        properties = read_properties(manifest)
        name = properties.get("app.0.name") or properties.get("name") or path.name
        apps.append((path.name, name, path.resolve()))
    return apps


def write_icon_pair(folder: Path, colour_svg: str, mono_svg: str, backup: bool = True):
    folder.mkdir(parents=True, exist_ok=True)
    written = []
    for name, content in (("icon.svg", colour_svg), ("icon_mono.svg", mono_svg)):
        target = folder / name
        if target.exists() and backup:
            try:
                target.with_suffix(target.suffix + ".bak").write_bytes(target.read_bytes())
            except OSError:
                pass
        # Validate before replacing anything.
        ET.fromstring(content)
        target.write_text(content, encoding="utf-8", newline="")
        written.append(target)
    return written


def load_text(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "utf-16"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("latin-1", "replace")


# ---------------------------------------------------------------------------
# Material Icons library (fonts + codepoints, converted to SVG)
# ---------------------------------------------------------------------------
def material_cache_directory(repo: Path | None) -> Path:
    base = (repo / ".tactility") if repo is not None else (Path.home() / ".tactility")
    return base / "icon-studio" / "material-icons"


def download_file(url: str, target: Path, timeout: float = 30.0) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": f"{APP_NAME}/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data = response.read()
    except (OSError, urllib.error.URLError) as error:
        raise IconError(
            f"Could not download {target.name}: {error}\n"
            f"You can also put the file in {target.parent}"
        ) from error
    if len(data) < 1024:
        raise IconError(f"Downloaded {target.name} looks empty")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f"{target.name}.", suffix=".part", dir=target.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
        temporary.replace(target)
    finally:
        try:
            temporary.unlink()
        except OSError:
            pass


def read_material_codepoints(path: Path) -> list[tuple[str, int]]:
    try:
        text = path.read_text(encoding="utf-8-sig")
    except OSError as error:
        raise IconError(f"Could not read {path.name}: {error}") from error
    entries: list[tuple[str, int]] = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            entries.append((parts[0], int(parts[1], 16)))
        except ValueError:
            continue
    if not entries:
        raise IconError(f"{path.name} has no icon names")
    return sorted(entries, key=lambda entry: entry[0])


def ensure_material_assets(cache: Path, style: str) -> tuple[Path, list[tuple[str, int]]]:
    """Download (once) and return the font + name/codepoint list for a style."""
    font_name, codepoints_name = MATERIAL_STYLES[style]
    font_path = cache / font_name
    codepoints_path = cache / codepoints_name

    if not font_path.is_file() or font_path.stat().st_size < 1024:
        download_file(MATERIAL_ICONS_BASE + urllib.parse.quote(font_name), font_path)
    if not codepoints_path.is_file() or codepoints_path.stat().st_size < 1024:
        download_file(MATERIAL_ICONS_BASE + urllib.parse.quote(codepoints_name), codepoints_path)

    if ImageFont is not None:
        try:
            ImageFont.truetype(str(font_path), 24)
        except OSError as error:
            try:
                font_path.unlink()
            except OSError:
                pass
            raise IconError(
                f"{font_name} is not a usable font ({error}). It was removed; try again to re-download it."
            ) from error

    return font_path, read_material_codepoints(codepoints_path)


def glyph_to_svg(font_path: Path, codepoint: int) -> str:
    """Convert one font glyph outline into a standalone, y-up-corrected SVG."""
    if TTFont is None or SVGPathPen is None or BoundsPen is None:
        raise IconError(
            "fontTools is needed to turn a font glyph into SVG.\n"
            "Install it with:  python -m pip install fonttools"
        )
    try:
        font = TTFont(str(font_path), fontNumber=0)
    except Exception as error:
        raise IconError(f"Could not open {font_path.name}: {error}") from error

    try:
        glyph_name = (font.getBestCmap() or {}).get(codepoint)
        if glyph_name is None:
            raise IconError(f"{font_path.name} has no glyph for U+{codepoint:04X}")
        glyph_set = font.getGlyphSet()
        glyph = glyph_set[glyph_name]

        bounds_pen = BoundsPen(glyph_set)
        glyph.draw(bounds_pen)
        if bounds_pen.bounds is None:
            raise IconError("This Material icon has no outline")
        x0, y0, x1, y1 = bounds_pen.bounds

        path_pen = SVGPathPen(glyph_set, ntos=fmt)
        glyph.draw(path_pen)
        data = path_pen.getCommands()
    finally:
        font.close()

    width, height = x1 - x0, y1 - y0
    if width <= 0 or height <= 0 or not data:
        raise IconError("This Material icon has no visible outline")
    return (
        f'<svg xmlns="{SVG_NS}" viewBox="{fmt(x0)} {fmt(-y1)} {fmt(width)} {fmt(height)}">'
        f'<g transform="scale(1 -1)"><path d="{data}" fill="{INK}"/></g></svg>'
    )


def render_material_glyph(font_path: Path, codepoint: int, size: int, colour: str,
                          background: str | None = None):
    if Image is None or ImageFont is None:
        raise IconError("Pillow is needed for glyph previews")
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    try:
        font = ImageFont.truetype(str(font_path), max(8, int(size * 0.86)))
        draw.text((size / 2.0, size / 2.0), chr(codepoint), font=font,
                  fill=(parse_color(colour) or RGBA(0, 0, 0)).tuple, anchor="mm")
    except (OSError, ValueError) as error:
        raise IconError(f"Could not draw this glyph: {error}") from error
    if background is not None:
        return flatten_preview(image, background)
    return image


# ---------------------------------------------------------------------------
# Material Icons picker
# ---------------------------------------------------------------------------
class MaterialBrowser(ctk.CTkToplevel if ctk is not None else object):
    COLUMNS = 8
    ROWS = 4
    CELL_WIDTH = 84
    CELL_HEIGHT = 76
    MARGIN = 10
    PAGE_SIZE = COLUMNS * ROWS

    def __init__(self, app, cache: Path, on_choose, initial_style: str = "Filled") -> None:
        super().__init__(app)
        self.app = app
        self.cache = cache
        self.on_choose = on_choose
        self.entries: list[tuple[str, int]] = []
        self.filtered: list[tuple[str, int]] = []
        self.page_entries: list[tuple[str, int]] = []
        self.page = 0
        self.selected: tuple[str, int] | None = None
        self.hover_index = -1
        self.font_path: Path | None = None
        self._pil_fonts: dict = {}
        self._grid_photo = None
        self._preview_photo = None
        self._filter_job = None
        self._load_job = None
        self._load_results: queue.Queue = queue.Queue()
        self._style_load_id = 0
        self._loading_style: str | None = None
        self._style_poll_job = None

        self.style_var = tk.StringVar(
            value=initial_style if initial_style in MATERIAL_STYLES else "Filled")
        self.search_var = tk.StringVar()

        self.title("Material Icons")
        self.geometry("788x690")
        self.minsize(748, 640)
        self.transient(app)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.bind("<Escape>", lambda _event: self._close())
        self.bind("<Control-f>", lambda _event: self.search_entry.focus_set())

        self._build_ui()
        self._center_on_parent()
        self._load_job = self.after(40, lambda: self._load_style(self.style_var.get()))

    @property
    def grid_width(self) -> int:
        return self.MARGIN * 2 + self.COLUMNS * self.CELL_WIDTH

    @property
    def grid_height(self) -> int:
        return self.MARGIN * 2 + self.ROWS * self.CELL_HEIGHT

    # ---- layout ----
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(2, weight=1)

        header = ctk.CTkFrame(self, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew", padx=18, pady=(16, 6))
        header.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(header, text="Material Icons",
                     font=ctk.CTkFont(size=20, weight="bold"), anchor="w").grid(
            row=0, column=0, sticky="w")
        ctk.CTkLabel(header, text="Pick a glyph to drop into the editor.",
                     font=ctk.CTkFont(size=12), text_color=("gray40", "gray60"),
                     anchor="w").grid(row=1, column=0, sticky="w")
        ctk.CTkOptionMenu(header, values=list(MATERIAL_STYLES), variable=self.style_var,
                          width=140, command=self._load_style).grid(
            row=0, column=1, rowspan=2, sticky="e")
        ctk.CTkButton(header, text="Reload", width=66, height=28, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=lambda: self._load_style(self.style_var.get())).grid(
            row=0, column=2, rowspan=2, sticky="e", padx=(6, 0))

        search = ctk.CTkFrame(self, fg_color="transparent")
        search.grid(row=1, column=0, sticky="ew", padx=18, pady=(2, 6))
        search.grid_columnconfigure(0, weight=1)
        self.search_entry = ctk.CTkEntry(search, placeholder_text="Search icons…",
                                         textvariable=self.search_var, height=32,
                                         text_color=("gray10", "gray90"))
        self.search_entry.grid(row=0, column=0, sticky="ew")
        self.search_entry.bind("<KeyRelease>", self._schedule_filter)
        self.search_var.trace_add("write", lambda *_args: self._schedule_filter())
        self.prev_button = ctk.CTkButton(search, text="‹", width=34,
                                         command=self._previous_page)
        self.prev_button.grid(row=0, column=1, padx=(8, 0))
        self.next_button = ctk.CTkButton(search, text="›", width=34,
                                         command=self._next_page)
        self.next_button.grid(row=0, column=2, padx=(4, 0))
        self.page_label = ctk.CTkLabel(search, text="Page 1 / 1", width=90, anchor="e",
                                       font=ctk.CTkFont(size=11),
                                       text_color=("gray40", "gray60"))
        self.page_label.grid(row=0, column=3, padx=(8, 0))

        content = ctk.CTkFrame(self, fg_color="transparent")
        content.grid(row=2, column=0, sticky="nsew", padx=18, pady=4)
        content.grid_columnconfigure(0, weight=1)
        content.grid_rowconfigure(0, weight=1)
        self.grid_label = ctk.CTkLabel(content, text="Loading…", width=self.grid_width,
                                       height=self.grid_height, fg_color="transparent",
                                       corner_radius=10)
        self.grid_label.grid(row=0, column=0)
        self.grid_label.bind("<Button-1>", self._on_click)
        self.grid_label.bind("<Double-Button-1>", self._on_double_click)
        self.grid_label.bind("<Motion>", self._on_motion)
        self.grid_label.bind("<Leave>", self._on_leave)

        footer = ctk.CTkFrame(self)
        footer.grid(row=3, column=0, sticky="ew", padx=18, pady=(8, 10))
        footer.grid_columnconfigure(2, weight=1)
        preview_card = ctk.CTkFrame(footer, width=104, height=104, corner_radius=10,
                                    fg_color=("#FFFFFF", "#181820"))
        preview_card.grid(row=0, column=0, rowspan=2, sticky="w")
        preview_card.grid_propagate(False)
        self.preview_label = ctk.CTkLabel(preview_card, text="", width=96, height=96,
                                          fg_color="transparent")
        self.preview_label.pack(padx=4, pady=4)
        self.selected_label = ctk.CTkLabel(footer, text="Pick a glyph", anchor="w",
                                           font=ctk.CTkFont(size=13, weight="bold"))
        self.selected_label.grid(row=0, column=1, columnspan=2, sticky="w",
                                 padx=(12, 8), pady=(6, 0))
        self.status = ctk.CTkLabel(footer, text="", anchor="w", justify="left",
                                   wraplength=420, font=ctk.CTkFont(size=11),
                                   text_color=("gray45", "gray60"))
        self.status.grid(row=1, column=1, columnspan=2, sticky="w", padx=(12, 8))
        self.use_button = ctk.CTkButton(footer, text="Use glyph", width=100,
                                        command=self._use_selected, state="disabled")
        self.use_button.grid(row=0, column=3, rowspan=2, padx=(8, 0))
        ctk.CTkButton(footer, text="Cancel", width=72, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=self._close).grid(row=0, column=4, rowspan=2, padx=(8, 0))

        license_row = ctk.CTkFrame(self, fg_color="transparent")
        license_row.grid(row=4, column=0, sticky="ew", padx=14, pady=(0, 10))
        ctk.CTkButton(license_row, text="Material Icons by Google · Apache 2.0", height=22,
                      fg_color="transparent", border_width=0, hover_color=("gray85", "gray20"),
                      text_color=("gray45", "gray60"),
                      command=lambda: webbrowser.open(MATERIAL_LICENSE_URL)).pack(side="left")
        ctk.CTkButton(license_row, text="Browse online ↗", height=22,
                      fg_color="transparent", border_width=0, hover_color=("gray85", "gray20"),
                      text_color=("gray45", "gray60"),
                      command=lambda: webbrowser.open(MATERIAL_SITE_URL)).pack(side="right")

    def _center_on_parent(self) -> None:
        self.update_idletasks()
        width, height = 788, 690
        parent = self.app.winfo_toplevel()
        x = parent.winfo_rootx() + max(0, (parent.winfo_width() - width) // 2)
        y = parent.winfo_rooty() + max(0, (parent.winfo_height() - height) // 2)
        self.geometry(f"{width}x{height}+{x}+{y}")
        self.lift()
        self.focus_force()

    # ---- data ----
    def _load_style(self, style: str) -> None:
        if not self.winfo_exists():
            return
        if self._load_job is not None:
            try:
                self.after_cancel(self._load_job)
            except tk.TclError:
                pass
            self._load_job = None
        self.style_var.set(style)
        self.selected = None
        self.hover_index = -1
        self.font_path = None
        self.entries = []
        self.filtered = []
        self.page_entries = []
        self.page = 0
        self._pil_fonts.clear()
        self._grid_photo = None
        self.grid_label.configure(image=None, text="Loading…",
                                  width=self.grid_width, height=self.grid_height)
        self.grid_label.image = None
        self.page_label.configure(text="Loading…")
        self.prev_button.configure(state="disabled")
        self.next_button.configure(state="disabled")
        self.use_button.configure(state="disabled")
        self.selected_label.configure(text="Pick a glyph")
        self.preview_label.configure(image=None, text="")
        font_name, codepoints_name = MATERIAL_STYLES[style]
        needs_download = (not (self.cache / font_name).is_file()
                          or not (self.cache / codepoints_name).is_file())
        self._status(f"Downloading {style} icons…" if needs_download else f"Loading {style}…")
        self.update_idletasks()

        self._style_load_id += 1
        token = self._style_load_id
        self._loading_style = style
        if self._style_poll_job is not None:
            try:
                self.after_cancel(self._style_poll_job)
            except tk.TclError:
                pass
        threading.Thread(target=self._load_style_worker, args=(style, token),
                         daemon=True).start()
        self._style_poll_job = self.after(60, self._poll_style_load)

    def _load_style_worker(self, style: str, token: int) -> None:
        try:
            font_path, entries = ensure_material_assets(self.cache, style)
            error = None
        except Exception as exc:  # noqa: BLE001 - report any loader failure to the UI
            font_path, entries, error = None, [], exc
        self._load_results.put((token, style, font_path, entries, error))

    def _poll_style_load(self) -> None:
        self._style_poll_job = None
        if not self.winfo_exists():
            return
        try:
            while True:
                token, style, font_path, entries, error = self._load_results.get_nowait()
                if token != self._style_load_id or style != self.style_var.get():
                    continue
                self._loading_style = None
                if error is not None:
                    self.font_path = None
                    self.entries = []
                    self.filtered = []
                    self.page_entries = []
                    self.page = 0
                    self._render_page()
                    self._status(str(error), error=True)
                    break
                self.font_path = font_path
                self.entries = entries
                self._pil_fonts.clear()
                self.page = 0
                self._apply_filter()
                suffix = "" if TTFont is not None else " · fontTools missing (pip install fonttools)"
                self._status(f"{len(entries)} icons · {style} · cached{suffix}", error=TTFont is None)
                if hasattr(self.app, "material_style"):
                    self.app.material_style = style
                    self.app.settings["material_style"] = style
                    self.app._save_later()
                break
        except queue.Empty:
            pass
        if self._loading_style is not None:
            self._style_poll_job = self.after(60, self._poll_style_load)

    def _schedule_filter(self, _event=None) -> None:
        if self._filter_job is not None:
            try:
                self.after_cancel(self._filter_job)
            except tk.TclError:
                pass
        self._filter_job = self.after(140, self._apply_filter)

    def _apply_filter(self) -> None:
        self._filter_job = None
        query = self.search_var.get().strip().lower().replace("_", " ").replace("-", " ")
        tokens = [token for token in query.split() if token]
        if tokens:
            self.filtered = [
                entry for entry in self.entries
                if all(token in entry[0].lower().replace("_", " ").replace("-", " ")
                       for token in tokens)
            ]
        else:
            self.filtered = list(self.entries)
        self.page = 0
        self._render_page()

    def _page_count(self) -> int:
        return max(1, math.ceil(len(self.filtered) / self.PAGE_SIZE))

    def _render_page(self) -> None:
        self.page = clamp(self.page, 0, self._page_count() - 1)
        start = self.page * self.PAGE_SIZE
        self.page_entries = self.filtered[start:start + self.PAGE_SIZE]
        self.page_label.configure(text=f"Page {self.page + 1} / {self._page_count()}")
        self.prev_button.configure(state="normal" if self.page > 0 else "disabled")
        self.next_button.configure(state="normal" if self.page + 1 < self._page_count() else "disabled")
        self.hover_index = -1
        self._render_grid()

    def _previous_page(self) -> None:
        if self.page > 0:
            self.page -= 1
            self._render_page()

    def _next_page(self) -> None:
        if self.page + 1 < self._page_count():
            self.page += 1
            self._render_page()

    # ---- rendering ----
    def _palette(self) -> dict[str, str]:
        mode = ctk.get_appearance_mode().lower()
        if mode == "system":
            try:
                import darkdetect

                dark = bool(darkdetect.isDark())
            except Exception:
                dark = True
        else:
            dark = mode != "light"
        if not dark:
            return {
                "background": "#F4F5F8", "hover": "#E4EAF4", "selected": "#3B82F6",
                "glyph": "#1A1A22", "selected_glyph": "#FFFFFF",
                "name": "#5B5F6B", "selected_name": "#FFFFFF", "preview": "#FFFFFF",
            }
        return {
            "background": "#111118", "hover": "#242430", "selected": "#2563EB",
            "glyph": "#E8E8F0", "selected_glyph": "#FFFFFF",
            "name": "#9DA0AE", "selected_name": "#FFFFFF", "preview": "#181820",
        }

    def _glyph_font(self, size: int):
        key = (str(self.font_path), size)
        if key not in self._pil_fonts:
            self._pil_fonts[key] = ImageFont.truetype(str(self.font_path), size)
        return self._pil_fonts[key]

    def _name_font(self):
        if "name" not in self._pil_fonts:
            try:
                self._pil_fonts["name"] = ImageFont.load_default(size=10)
            except TypeError:  # older Pillow
                self._pil_fonts["name"] = ImageFont.load_default()
        return self._pil_fonts["name"]

    def _render_grid(self) -> None:
        colors = self._palette()
        if Image is None:
            self.grid_label.configure(image=None, text="Pillow is required for previews",
                                      width=self.grid_width, height=self.grid_height)
            return

        image = Image.new("RGB", (self.grid_width, self.grid_height), colors["background"])
        draw = ImageDraw.Draw(image)

        if not self.page_entries:
            draw.text((self.grid_width / 2.0, self.grid_height / 2.0),
                      "No icons match" if self.entries else "No icons loaded",
                      font=self._name_font(), fill=colors["name"], anchor="mm")
        elif self.font_path is not None:
            try:
                glyph_font = self._glyph_font(36)
                name_font = self._name_font()
            except (OSError, ValueError) as error:
                self._status(f"Could not load the icon font: {error}", error=True)
                return
            for index, (name, codepoint) in enumerate(self.page_entries):
                column = index % self.COLUMNS
                row = index // self.COLUMNS
                x = self.MARGIN + column * self.CELL_WIDTH
                y = self.MARGIN + row * self.CELL_HEIGHT
                is_selected = self.selected == (name, codepoint)
                is_hovered = index == self.hover_index

                if is_selected or is_hovered:
                    draw.rounded_rectangle(
                        (x + 2, y + 2, x + self.CELL_WIDTH - 2, y + self.CELL_HEIGHT - 2),
                        radius=8,
                        fill=colors["selected"] if is_selected else colors["hover"],
                    )

                glyph_colour = colors["selected_glyph"] if is_selected else colors["glyph"]
                try:
                    draw.text((x + self.CELL_WIDTH / 2.0, y + 27),
                              chr(codepoint), font=glyph_font, fill=glyph_colour, anchor="mm")
                except (OSError, ValueError):
                    pass

                label = name.replace("_", " ")
                if len(label) > 15:
                    label = label[:14].rstrip() + "…"
                name_colour = colors["selected_name"] if is_selected else colors["name"]
                draw.text((x + self.CELL_WIDTH / 2.0, y + 57),
                          label, font=name_font, fill=name_colour, anchor="mm")

        photo = ctk.CTkImage(light_image=image, dark_image=image,
                             size=(self.grid_width, self.grid_height))
        self.grid_label.configure(image=photo, text="",
                                  width=self.grid_width, height=self.grid_height)
        self.grid_label.image = photo
        self._grid_photo = photo

    def _cell_index(self, x: int, y: int) -> int | None:
        x -= self.MARGIN
        y -= self.MARGIN
        if x < 0 or y < 0:
            return None
        column = int(x // self.CELL_WIDTH)
        row = int(y // self.CELL_HEIGHT)
        if column >= self.COLUMNS or row >= self.ROWS:
            return None
        index = row * self.COLUMNS + column
        if index >= len(self.page_entries):
            return None
        return index

    def _on_click(self, event) -> None:
        index = self._cell_index(event.x, event.y)
        if index is not None:
            self._select(self.page_entries[index])

    def _on_double_click(self, event) -> None:
        index = self._cell_index(event.x, event.y)
        if index is not None:
            self._select(self.page_entries[index])
            self._use_selected()

    def _on_motion(self, event) -> None:
        index = self._cell_index(event.x, event.y)
        if index != self.hover_index:
            self.hover_index = index
            self._render_grid()

    def _on_leave(self, _event=None) -> None:
        if self.hover_index != -1:
            self.hover_index = -1
            self._render_grid()

    def _select(self, entry: tuple[str, int]) -> None:
        self.selected = entry
        name, codepoint = entry
        self.selected_label.configure(text=f"{name}  ·  U+{codepoint:04X}")
        self.use_button.configure(
            state="normal" if self.font_path is not None and TTFont is not None else "disabled")
        if self.font_path is not None:
            try:
                colors = self._palette()
                image = render_material_glyph(self.font_path, codepoint, 96,
                                              colors["glyph"], colors["preview"])
                photo = ctk.CTkImage(light_image=image, dark_image=image, size=(96, 96))
                self.preview_label.configure(image=photo, text="")
                self.preview_label.image = photo
                self._preview_photo = photo
            except (IconError, OSError, ValueError) as error:
                self._status(str(error), error=True)
        self._render_grid()

    def _use_selected(self) -> None:
        if self.selected is None or self.font_path is None:
            return
        name, codepoint = self.selected
        try:
            svg = glyph_to_svg(self.font_path, codepoint)
        except IconError as error:
            self._status(str(error), error=True)
            return
        self._close()
        self.on_choose(svg, name)

    def _status(self, text: str, error: bool = False) -> None:
        self.status.configure(text=text,
                              text_color=("#b23", "#ff8787") if error else ("gray45", "gray60"))

    def _close(self) -> None:
        if self._filter_job is not None:
            try:
                self.after_cancel(self._filter_job)
            except tk.TclError:
                pass
            self._filter_job = None
        if self._load_job is not None:
            try:
                self.after_cancel(self._load_job)
            except tk.TclError:
                pass
            self._load_job = None
        if self._style_poll_job is not None:
            try:
                self.after_cancel(self._style_poll_job)
            except tk.TclError:
                pass
            self._style_poll_job = None
        try:
            self.grab_release()
        except tk.TclError:
            pass
        self.destroy()


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
class IconStudio(ctk.CTk if ctk is not None else object):
    def __init__(self, repo: Path | None) -> None:
        super().__init__()
        self.repo = repo
        self.settings_file = ((repo / ".tactility" / "icon-studio.json") if repo
                              else Path.home() / ".tactility" / "icon-studio.json")
        self.settings = self._load_settings()
        self.apps = scan_apps(repo)
        self.material_cache = material_cache_directory(repo)
        self.material_style = (self.settings.get("material_style", "Filled")
                               if self.settings.get("material_style", "Filled") in MATERIAL_STYLES
                               else "Filled")
        self.icon: ParsedIcon | None = None
        self.source_text = ""
        self.source_name = ""
        self.result: BuildResult | None = None
        self._refresh_job = None
        self._save_job = None
        self._auto_switched = False
        self._image_refs: dict[str, object] = {}
        self._swatches: dict[str, ctk.CTkButton] = {}
        self._preview_labels: dict[str, ctk.CTkLabel] = {}

        ctk.set_appearance_mode(self.settings.get("appearance", "Dark"))
        ctk.set_default_color_theme("blue")

        self.title(APP_NAME)
        self.geometry(self._initial_geometry())
        self.minsize(1120, 720)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.bg_var = tk.StringVar(value=self.settings.get("background", DEFAULT_BG))
        self.glyph_mode_var = tk.StringVar(
            value=GLYPH_MODE_LABELS.get(self.settings.get("glyph_mode", "auto"), "Auto contrast"))
        self.glyph_var = tk.StringVar(value=self.settings.get("glyph_colour", DEFAULT_GLYPH))
        self.colour_size_var = tk.DoubleVar(value=float(self.settings.get("colour_size", DEFAULT_COLOUR_SIZE)))
        self.mono_size_var = tk.DoubleVar(value=float(self.settings.get("mono_size", DEFAULT_MONO_SIZE)))
        self.min_stroke_var = tk.DoubleVar(value=float(self.settings.get("min_stroke", DEFAULT_MIN_STROKE)))
        self.keep_layout_var = tk.BooleanVar(value=bool(self.settings.get("keep_layout", False)))
        self.backup_var = tk.BooleanVar(value=bool(self.settings.get("backup", True)))
        self.folder_var = tk.StringVar(value=self.settings.get("folder", ""))
        self.app_var = tk.StringVar(value="")
        self.appearance_var = tk.StringVar(value=self.settings.get("appearance", "Dark"))

        self._build_ui()
        self._populate_apps()
        self._restore_last_source()
        self.bind("<Control-s>", lambda _event: self._save_both())
        self.bind("<Control-o>", lambda _event: self._browse_source())
        self.bind("<Control-m>", lambda _event: self._browse_material_icons())
        self.bind("<F5>", lambda _event: self._schedule_refresh(immediate=True))
        self.after(200, self._schedule_refresh)

    # ---- settings ----
    def _load_settings(self) -> dict:
        defaults = {
            "appearance": "Dark",
            "geometry": "",
            "background": DEFAULT_BG,
            "glyph_mode": "auto",
            "glyph_colour": DEFAULT_GLYPH,
            "colour_size": DEFAULT_COLOUR_SIZE,
            "mono_size": DEFAULT_MONO_SIZE,
            "min_stroke": DEFAULT_MIN_STROKE,
            "keep_layout": False,
            "material_style": "Filled",
            "backup": True,
            "folder": "",
            "last_svg": "",
        }
        try:
            data = json.loads(self.settings_file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return defaults
        if isinstance(data, dict):
            defaults.update({key: data[key] for key in defaults if key in data})
        return defaults

    def _initial_geometry(self) -> str:
        saved = str(self.settings.get("geometry") or "")
        match = re.match(r"^(\d+)x(\d+)\+(-?\d+)\+(-?\d+)$", saved)
        if match and int(match.group(1)) >= 1000 and int(match.group(2)) >= 640:
            return saved
        return "1280x820"

    def _save_settings(self) -> None:
        self._save_job = None
        geometry = str(self.settings.get("geometry") or "")
        try:
            if self.winfo_ismapped() and self.winfo_width() >= 1000 and self.winfo_height() >= 640:
                geometry = self.geometry()
        except tk.TclError:
            pass
        self.settings.update({
            "appearance": self.appearance_var.get(),
            "geometry": geometry,
            "background": normalize_hex(self.bg_var.get(), DEFAULT_BG),
            "glyph_mode": GLYPH_MODES.get(self.glyph_mode_var.get(), "auto"),
            "glyph_colour": normalize_hex(self.glyph_var.get(), DEFAULT_GLYPH),
            "colour_size": float(self.colour_size_var.get()),
            "mono_size": float(self.mono_size_var.get()),
            "min_stroke": float(self.min_stroke_var.get()),
            "keep_layout": bool(self.keep_layout_var.get()),
            "material_style": self.material_style,
            "backup": bool(self.backup_var.get()),
            "folder": self.folder_var.get().strip(),
            "last_svg": self.source_name if self.source_name.lower().endswith(".svg") else "",
        })
        try:
            self.settings_file.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.settings_file.with_suffix(".tmp")
            temporary.write_text(json.dumps(self.settings, indent=2), encoding="utf-8")
            temporary.replace(self.settings_file)
        except OSError:
            pass

    def _save_later(self) -> None:
        if self._save_job is not None:
            self.after_cancel(self._save_job)
        self._save_job = self.after(500, self._save_settings)

    # ---- layout ----
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, minsize=452, weight=0)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self.controls = ctk.CTkScrollableFrame(self, width=436, corner_radius=0,
                                               fg_color=("gray90", "gray13"))
        self.controls.grid(row=0, column=0, sticky="nsew")
        self.controls.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self.controls, text=APP_NAME,
                     font=ctk.CTkFont(size=22, weight="bold"), anchor="w").pack(
            fill="x", padx=18, pady=(18, 0))
        subtitle = self.repo.name if self.repo else "no checkout found"
        ctk.CTkLabel(self.controls, text=subtitle, font=ctk.CTkFont(size=12),
                     text_color=("gray40", "gray60"), anchor="w").pack(
            fill="x", padx=18, pady=(0, 14))

        self._source_card()
        self._colour_card()
        self._mono_card()
        self._output_card()

        self.status = ctk.CTkLabel(self.controls, text="Paste an SVG to begin.", anchor="w",
                                   justify="left", wraplength=390,
                                   text_color=("gray35", "gray65"))
        self.status.pack(fill="x", padx=18, pady=(2, 18))

        self._build_previews()

    def _card(self, title: str):
        card = ctk.CTkFrame(self.controls)
        card.pack(fill="x", padx=14, pady=(0, 10))
        header = ctk.CTkFrame(card, fg_color="transparent")
        header.pack(fill="x", padx=16, pady=(12, 2))
        ctk.CTkLabel(header, text=title, font=ctk.CTkFont(size=13, weight="bold"),
                     anchor="w").pack(side="left")
        body = ctk.CTkFrame(card, fg_color="transparent")
        body.pack(fill="x", padx=16, pady=(4, 14))
        body.grid_columnconfigure(1, weight=1)
        return body

    def _source_card(self) -> None:
        body = self._card("Source glyph")

        row = ctk.CTkFrame(body, fg_color="transparent")
        row.grid(row=0, column=0, columnspan=3, sticky="ew")
        row.grid_columnconfigure(0, weight=1)
        self.path_entry = ctk.CTkEntry(row, placeholder_text="SVG file…", height=30)
        self.path_entry.grid(row=0, column=0, sticky="ew")
        ctk.CTkButton(row, text="Browse", width=72, command=self._browse_source).grid(
            row=0, column=1, padx=(8, 0))
        ctk.CTkButton(row, text="Load", width=58, command=self._load_path_entry).grid(
            row=0, column=2, padx=(6, 0))

        self.source_box = ctk.CTkTextbox(body, height=104, wrap="none",
                                         font=ctk.CTkFont(family=self._mono_font(), size=11))
        self.source_box.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 6))

        actions = ctk.CTkFrame(body, fg_color="transparent")
        actions.grid(row=2, column=0, columnspan=3, sticky="ew")
        ctk.CTkButton(actions, text="Use pasted SVG", width=132,
                      command=self._use_pasted).pack(side="left")
        ctk.CTkButton(actions, text="Browse icons", width=110,
                      command=self._browse_material_icons).pack(side="left", padx=(8, 0))
        ctk.CTkButton(actions, text="Clear", width=64, fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=lambda: self.source_box.delete("1.0", "end")).pack(
            side="left", padx=(8, 0))

        info_row = ctk.CTkFrame(body, fg_color="transparent")
        info_row.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        info_row.grid_columnconfigure(0, weight=1)
        self.source_info = ctk.CTkLabel(info_row, text="Paste an SVG, or browse the Material Icons library.",
                                        anchor="w", justify="left", wraplength=282,
                                        font=ctk.CTkFont(size=11),
                                        text_color=("gray45", "gray60"))
        self.source_info.grid(row=0, column=0, sticky="w")
        ctk.CTkButton(info_row, text="SVGViewer ↗", width=94, height=24,
                      fg_color="transparent", border_width=0, hover_color=("gray85", "gray20"),
                      text_color=("gray45", "gray60"),
                      command=lambda: webbrowser.open("https://www.svgviewer.dev")).grid(
            row=0, column=1, sticky="e")

    def _colour_card(self) -> None:
        body = self._card("Colour icon")

        self._colour_row(body, 0, "Background", self.bg_var)
        palette = ctk.CTkFrame(body, fg_color="transparent")
        palette.grid(row=1, column=0, columnspan=3, sticky="w", pady=(6, 4))
        for colour in BG_PALETTE:
            ctk.CTkButton(palette, text="", width=20, height=20, corner_radius=4,
                          fg_color=colour, hover_color=colour, border_width=1,
                          border_color=("gray65", "gray45"),
                          command=lambda value=colour: self._set_colour(self.bg_var, value)).pack(
                side="left", padx=(0, 4))

        ctk.CTkLabel(body, text="Glyph", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), anchor="w").grid(
            row=2, column=0, sticky="w", pady=(8, 4))
        ctk.CTkOptionMenu(body, values=list(GLYPH_MODES), variable=self.glyph_mode_var,
                          width=170, command=lambda _value: self._on_glyph_mode()).grid(
            row=2, column=1, columnspan=2, sticky="w", pady=(8, 4))

        self._glyph_widgets = self._colour_row(body, 3, "Glyph colour", self.glyph_var)
        self.colour_size_slider = self._slider_row(body, 4, "Glyph size", self.colour_size_var,
                                                   40, 92, "colour_size_label", "% of canvas")
        self.layout_switch = ctk.CTkSwitch(body, text="Keep source layout (full-bleed)",
                                           variable=self.keep_layout_var,
                                           command=self._layout_changed)
        self.layout_switch.grid(row=5, column=0, columnspan=3, sticky="w", pady=(4, 0))
        self.layout_switch.configure(state="disabled")
        self._on_glyph_mode()
    def _mono_card(self) -> None:
        body = self._card("Mono icon")
        self._slider_row(body, 0, "Glyph size", self.mono_size_var, 40, 92,
                         "mono_size_label", "% of canvas")
        self._slider_row(body, 1, "Min stroke", self.min_stroke_var, 0, 8,
                         "min_stroke_label", "units at 96 px", resolution=0.5)
        ctk.CTkLabel(body, text="Pure black on transparent. The launcher tints it.",
                     anchor="w", font=ctk.CTkFont(size=11),
                     text_color=("gray45", "gray60")).grid(
            row=2, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def _output_card(self) -> None:
        body = self._card("Output")

        ctk.CTkLabel(body, text="App", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), anchor="w").grid(
            row=0, column=0, sticky="w", pady=4)
        self.app_menu = ctk.CTkOptionMenu(body, values=["(none)"], width=150,
                                          command=self._on_app_selected)
        self.app_menu.grid(row=0, column=1, sticky="w", pady=4)
        ctk.CTkButton(body, text="Refresh", width=70, height=26, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=self._reload_apps).grid(row=0, column=2, sticky="e", pady=4)

        ctk.CTkLabel(body, text="Folder", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), anchor="w").grid(
            row=1, column=0, sticky="w", pady=4)
        folder_row = ctk.CTkFrame(body, fg_color="transparent")
        folder_row.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        folder_row.grid_columnconfigure(0, weight=1)
        ctk.CTkEntry(folder_row, textvariable=self.folder_var, height=30).grid(
            row=0, column=0, sticky="ew")
        ctk.CTkButton(folder_row, text="…", width=32, command=self._browse_output).grid(
            row=0, column=1, padx=(6, 0))

        ctk.CTkSwitch(body, text="Back up existing icons (.bak)", variable=self.backup_var,
                      command=self._save_later).grid(
            row=2, column=0, columnspan=3, sticky="w", pady=(6, 8))

        buttons = ctk.CTkFrame(body, fg_color="transparent")
        buttons.grid(row=3, column=0, columnspan=3, sticky="ew")
        buttons.grid_columnconfigure(0, weight=1)
        buttons.grid_columnconfigure(1, weight=1)
        ctk.CTkButton(buttons, text="Save both", command=self._save_both).grid(
            row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        ctk.CTkButton(buttons, text="Save colour", fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=lambda: self._save_one("colour")).grid(
            row=1, column=0, sticky="ew", padx=(0, 4))
        ctk.CTkButton(buttons, text="Save mono", fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=lambda: self._save_one("mono")).grid(
            row=1, column=1, sticky="ew", padx=(4, 0))
        ctk.CTkButton(buttons, text="Copy colour SVG", height=26, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=lambda: self._copy("colour")).grid(
            row=2, column=0, sticky="ew", padx=(0, 4), pady=(6, 0))
        ctk.CTkButton(buttons, text="Copy mono SVG", height=26, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=lambda: self._copy("mono")).grid(
            row=2, column=1, sticky="ew", padx=(4, 0), pady=(6, 0))

    def _colour_row(self, body, row: int, label: str, variable) -> tuple:
        ctk.CTkLabel(body, text=label, font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        holder = ctk.CTkFrame(body, fg_color="transparent")
        holder.grid(row=row, column=1, columnspan=2, sticky="ew", pady=4)
        holder.grid_columnconfigure(1, weight=1)

        swatch = ctk.CTkButton(holder, text="", width=28, height=28, corner_radius=5,
                               fg_color=normalize_hex(variable.get(), DEFAULT_BG),
                               border_width=1, border_color=("gray60", "gray45"),
                               hover_color=normalize_hex(variable.get(), DEFAULT_BG),
                               command=lambda: self._pick_colour(variable))
        swatch.grid(row=0, column=0, padx=(0, 6))
        entry = ctk.CTkEntry(holder, textvariable=variable, width=100, height=28)
        entry.grid(row=0, column=1, sticky="w")
        entry.bind("<KeyRelease>", lambda _event: self._colour_typed(variable))
        pick = ctk.CTkButton(holder, text="Pick…", width=58, height=28, fg_color="transparent",
                             border_width=1, text_color=("gray10", "gray90"),
                             border_color=("gray60", "gray40"),
                             command=lambda: self._pick_colour(variable))
        pick.grid(row=0, column=2, padx=(6, 0))
        self._swatches[str(variable)] = swatch
        return swatch, entry, pick

    def _slider_row(self, body, row: int, label: str, variable, low: float, high: float,
                    label_key: str, suffix: str, resolution: float = 1.0):
        ctk.CTkLabel(body, text=label, font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        value_label = ctk.CTkLabel(body, text="", width=92, anchor="e",
                                   font=ctk.CTkFont(size=11),
                                   text_color=("gray35", "gray65"))
        value_label.grid(row=row, column=2, sticky="e", pady=4)
        self._image_refs[label_key] = value_label

        slider = ctk.CTkSlider(body, from_=low, to=high, number_of_steps=int((high - low) / resolution),
                               command=lambda value, var=variable, key=label_key, text=suffix:
                               self._slider_changed(var, value, key, text))
        slider.grid(row=row, column=1, sticky="ew", padx=(8, 8), pady=4)
        slider.set(float(variable.get()))
        self._slider_changed(variable, variable.get(), label_key, suffix)
        return slider

    def _slider_changed(self, variable, value, label_key: str, suffix: str) -> None:
        variable.set(float(value))
        label = self._image_refs.get(label_key)
        if isinstance(label, ctk.CTkLabel):
            label.configure(text=f"{float(value):g} {suffix}")
        self._schedule_refresh()
        self._save_later()

    def _build_previews(self) -> None:
        panel = ctk.CTkFrame(self, fg_color="transparent")
        panel.grid(row=0, column=1, sticky="nsew", padx=18, pady=16)
        panel.grid_columnconfigure(0, weight=1)
        panel.grid_rowconfigure(1, weight=1)

        header = ctk.CTkFrame(panel, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew")
        header.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(header, text="Preview", font=ctk.CTkFont(size=20, weight="bold"),
                     anchor="w").grid(row=0, column=0, sticky="w")
        ctk.CTkOptionMenu(header, values=["Dark", "Light", "System"], width=110,
                          variable=self.appearance_var,
                          command=self._appearance_changed).grid(row=0, column=1, sticky="e")

        tabs = ctk.CTkTabview(panel)
        tabs.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        self.preview_tabs = tabs
        tabs.add("Colour")
        tabs.add("Mono")
        tabs.add("At 24 px")

        colour_tab = tabs.tab("Colour")
        self._preview_row(colour_tab, [
            ("colour_dark", "Colour · dark background", DARK_CARD, PREVIEW_SIZE, DARK_INK),
            ("colour_light", "Colour · light background", LIGHT_CARD, PREVIEW_SIZE, LIGHT_INK),
        ])

        mono_tab = tabs.tab("Mono")
        self._preview_row(mono_tab, [
            ("mono_dark", "Mono · dark theme (tinted)", DARK_CARD, PREVIEW_SIZE, DARK_INK),
            ("mono_light", "Mono · light theme (tinted)", LIGHT_CARD, PREVIEW_SIZE, LIGHT_INK),
        ])

        small_tab = tabs.tab("At 24 px")
        self._preview_row(small_tab, [
            ("small_colour_dark", "Colour 24 px · dark", DARK_CARD, 132, DARK_INK),
            ("small_colour_light", "Colour 24 px · light", LIGHT_CARD, 132, LIGHT_INK),
            ("small_mono_dark", "Mono 24 px · dark", DARK_CARD, 132, DARK_INK),
            ("small_mono_light", "Mono 24 px · light", LIGHT_CARD, 132, LIGHT_INK),
        ], columns=4)

        diagnostics = ctk.CTkFrame(panel, fg_color=("gray88", "gray15"), corner_radius=8)
        diagnostics.grid(row=2, column=0, sticky="ew", pady=(12, 0))
        self.diagnostics = ctk.CTkLabel(
            diagnostics, text="", anchor="w", justify="left", wraplength=600,
            font=ctk.CTkFont(family=self._mono_font(), size=11),
            text_color=("gray30", "gray70"))
        self.diagnostics.pack(fill="x", padx=14, pady=10)

    def _preview_row(self, parent, entries, columns: int | None = None) -> None:
        row = ctk.CTkFrame(parent, fg_color="transparent")
        row.pack(fill="both", expand=True, pady=4)
        columns = columns or len(entries)
        for column in range(columns):
            row.grid_columnconfigure(column, weight=1)
        row.grid_rowconfigure(0, weight=1)

        for index, (key, title, background, size, ink) in enumerate(entries):
            card = ctk.CTkFrame(row, fg_color=background, corner_radius=10)
            card.grid(row=0, column=index, padx=6, pady=4)
            ctk.CTkLabel(card, text=title, font=ctk.CTkFont(size=11),
                         text_color=ink, fg_color="transparent").pack(padx=10, pady=(8, 2))
            label = ctk.CTkLabel(card, text="", width=size, height=size,
                                 fg_color="transparent")
            label.pack(padx=10, pady=(0, 4))
            self._preview_labels[key] = label
            self._image_refs[key] = label

    # ---- source handling ----
    @staticmethod
    def _mono_font() -> str:
        if sys.platform == "darwin":
            return "Menlo"
        if os.name == "nt":
            return "Consolas"
        return "DejaVu Sans Mono"

    def _browse_source(self) -> None:
        path = filedialog.askopenfilename(
            title="Choose an SVG glyph",
            filetypes=[("SVG files", "*.svg"), ("All files", "*.*")],
        )
        if not path:
            return
        self.path_entry.delete(0, "end")
        self.path_entry.insert(0, path)
        self._load_source_file(Path(path))

    def _load_path_entry(self) -> None:
        text = self.path_entry.get().strip().strip('"')
        if not text:
            self._browse_source()
            return
        self._load_source_file(Path(text))

    def _load_source_file(self, path: Path) -> None:
        try:
            text = load_text(path)
        except OSError as error:
            self._status(f"Could not read {path.name}: {error}", error=True)
            return
        self._set_source(text, str(path))

    def _use_pasted(self) -> None:
        text = self.source_box.get("1.0", "end").strip()
        if not text:
            self._status("Paste an SVG document or path data first.", error=True)
            return
        self._set_source(text, "")

    def _browse_material_icons(self) -> None:
        if ctk is None:
            return
        if Image is None or ImageFont is None:
            self._status("Pillow is required for the Material Icons browser.", error=True)
            return
        for child in self.winfo_children():
            if isinstance(child, MaterialBrowser) and child.winfo_exists():
                child.lift()
                child.focus_force()
                return
        MaterialBrowser(self, self.material_cache, self._use_material_glyph,
                        initial_style=self.material_style)

    def _use_material_glyph(self, svg: str, name: str) -> None:
        # A font glyph is a single-colour shape, so auto contrast is the
        # sensible default unless the user deliberately picked a solid colour.
        if GLYPH_MODES.get(self.glyph_mode_var.get()) in ("auto", "source"):
            self.glyph_mode_var.set(GLYPH_MODE_LABELS["auto"])
            self._on_glyph_mode()
        self._set_source(svg, f"material:{name}")
        if self.source_name == f"material:{name}":
            info = self.source_info.cget("text")
            self.source_info.configure(text=f"Material Icons / {name} · {info}")
            self._status(f"Loaded Material icon '{name}'")

    def _set_source(self, text: str, name: str) -> None:
        try:
            icon = parse_document(text, source_name=name)
        except IconError as error:
            self._status(str(error), error=True)
            return

        multi_colour = len({paint.lower() for paint in icon.paints}) > 1 or icon.uses_gradient
        mode = GLYPH_MODES.get(self.glyph_mode_var.get())
        if multi_colour and mode == "auto":
            self.glyph_mode_var.set(GLYPH_MODE_LABELS["source"])
            self._auto_switched = True
        elif not multi_colour and self._auto_switched:
            self.glyph_mode_var.set(GLYPH_MODE_LABELS["auto"])
            self._auto_switched = False

        self.icon = icon
        self.result = None
        self.source_text = text
        self.source_name = name
        self.source_box.delete("1.0", "end")
        self.source_box.insert("1.0", text)

        if icon.background:
            self.bg_var.set(icon.background)
            self._colour_typed(self.bg_var)
            self.keep_layout_var.set(True)
        else:
            self.keep_layout_var.set(False)

        self._apply_layout_state()

        view = icon.viewbox
        info = [f"{len(icon.shapes)} shape(s) · viewBox {fmt(view[0])} {fmt(view[1])} {fmt(view[2])} {fmt(view[3])}"]
        if icon.background:
            info.append(f"background {icon.background}")
        if icon.unsupported:
            info.append("dropped: " + ", ".join(sorted(icon.unsupported)))
        self.source_info.configure(text=" · ".join(info))
        self._status("Loaded " + (Path(name).name if name else "pasted SVG"))
        self._schedule_refresh(immediate=True)
        self._save_later()

    def _restore_last_source(self) -> None:
        last = str(self.settings.get("last_svg") or "")
        if last and Path(last).is_file():
            self.path_entry.insert(0, last)
            self._load_source_file(Path(last))
        if not self.folder_var.get() and self.apps:
            self.folder_var.set(str(self.apps[0][2]))
            self.app_var.set(self.apps[0][0])
        self._sync_app_menu()

    def _on_glyph_mode(self) -> None:
        self._auto_switched = False
        enabled = GLYPH_MODES.get(self.glyph_mode_var.get()) == "solid"
        for widget in getattr(self, "_glyph_widgets", ()):
            widget.configure(state="normal" if enabled else "disabled")
        self._schedule_refresh()
        self._save_later()

    def _layout_changed(self) -> None:
        self._apply_layout_state()
        self._schedule_refresh()
        self._save_later()

    def _apply_layout_state(self) -> None:
        available = bool(self.icon and self.icon.background)
        keep = available and bool(self.keep_layout_var.get())
        switch = getattr(self, "layout_switch", None)
        if switch is not None:
            switch.configure(state="normal" if available else "disabled")
        slider = getattr(self, "colour_size_slider", None)
        if slider is not None:
            slider.configure(state="disabled" if keep else "normal")
        label = self._image_refs.get("colour_size_label")
        if isinstance(label, ctk.CTkLabel):
            label.configure(text="full canvas" if keep else f"{float(self.colour_size_var.get()):g} % of canvas")

    # ---- colours ----
    def _set_colour(self, variable, colour: str) -> None:
        variable.set(normalize_hex(colour))
        swatch = self._swatches.get(str(variable))
        if swatch is not None:
            swatch.configure(fg_color=normalize_hex(colour), hover_color=normalize_hex(colour))
        self._schedule_refresh()
        self._save_later()

    def _pick_colour(self, variable) -> None:
        _rgb, value = colorchooser.askcolor(color=normalize_hex(variable.get()),
                                            parent=self, title="Choose colour")
        if value:
            self._set_colour(variable, value)

    def _colour_typed(self, variable) -> None:
        parsed = parse_color(variable.get())
        if parsed is None:
            self._save_later()
            return
        swatch = self._swatches.get(str(variable))
        if swatch is not None:
            swatch.configure(fg_color=parsed.hex, hover_color=parsed.hex)
        self._schedule_refresh()
        self._save_later()

    # ---- apps / output ----
    def _populate_apps(self) -> None:
        folders = [folder for folder, _name, _path in self.apps]
        labels = ["(none)"] + folders
        self.app_menu.configure(values=labels)
        current = self.app_var.get()
        if current in labels:
            self.app_menu.set(current)
        else:
            self.app_menu.set(labels[0])
            self.app_var.set(labels[0])
            if self.apps and not self.folder_var.get():
                self.folder_var.set(str(self.apps[0][2]))

    def _reload_apps(self) -> None:
        self.apps = scan_apps(self.repo)
        self._populate_apps()
        self._status(f"Found {len(self.apps)} app(s)")

    def _on_app_selected(self, value: str) -> None:
        for folder, _name, path in self.apps:
            if folder == value:
                self.folder_var.set(str(path))
                break
        self._save_later()

    def _sync_app_menu(self) -> None:
        text = self.folder_var.get().strip().strip('"')
        if not text:
            self.app_var.set("(none)")
            self.app_menu.set("(none)")
            return
        try:
            folder = Path(text).expanduser().resolve()
        except OSError:
            return
        for name, _label, path in self.apps:
            if path == folder:
                self.app_var.set(name)
                self.app_menu.set(name)
                return
        self.app_var.set("(none)")
        self.app_menu.set("(none)")

    def _browse_output(self) -> None:
        initial = self.folder_var.get().strip() or (str(self.repo) if self.repo else "")
        path = filedialog.askdirectory(title="Icon output folder", initialdir=initial or None)
        if path:
            self.folder_var.set(path)
            self._sync_app_menu()
            self._save_later()

    def _output_folder(self) -> Path | None:
        text = self.folder_var.get().strip().strip('"')
        if not text:
            self._status("Choose an output folder first.", error=True)
            return None
        return Path(text).expanduser()

    # ---- render ----
    def _schedule_refresh(self, immediate: bool = False) -> None:
        if self._refresh_job is not None:
            try:
                self.after_cancel(self._refresh_job)
            except tk.TclError:
                pass
            self._refresh_job = None
        self._refresh_job = self.after(0 if immediate else 90, self._refresh)

    def _icon_options(self) -> IconOptions:
        return IconOptions(
            background=normalize_hex(self.bg_var.get(), DEFAULT_BG),
            glyph_mode=GLYPH_MODES.get(self.glyph_mode_var.get(), "auto"),
            glyph_colour=normalize_hex(self.glyph_var.get(), DEFAULT_GLYPH),
            colour_size=float(self.colour_size_var.get()),
            mono_size=float(self.mono_size_var.get()),
            min_stroke=float(self.min_stroke_var.get()),
            keep_layout=bool(self.keep_layout_var.get()),
        )

    def _ensure_current(self) -> bool:
        """Regenerate synchronously before saving/copying, cancelling the debounce."""
        if self.icon is None:
            self.result = None
            return False
        if self._refresh_job is not None:
            try:
                self.after_cancel(self._refresh_job)
            except tk.TclError:
                pass
            self._refresh_job = None
        self._refresh()
        return self.result is not None

    def _refresh(self) -> None:
        self._refresh_job = None
        self.result = None
        if self.icon is None:
            self._set_diagnostics("Load an SVG to build the two icons.")
            return

        try:
            self.result = build_outputs(self.icon, self._icon_options())
        except IconError as error:
            self._status(str(error), error=True)
            return

        if Image is None:
            self._set_diagnostics("Pillow is missing, so previews are unavailable.")
            return

        try:
            colour_icon = parse_document(self.result.colour_svg, strip_background=False)
            mono_icon = parse_document(self.result.mono_svg, strip_background=False)
            colour_big = render_parsed(colour_icon, PREVIEW_RENDER)
            mono_big = render_parsed(mono_icon, PREVIEW_RENDER)
            colour_small = render_parsed(colour_icon, SMALL_SIZE)
            mono_small = render_parsed(mono_icon, SMALL_SIZE)
        except (IconError, ValueError) as error:
            self._status(f"Preview failed: {error}", error=True)
            return

        self._show("colour_dark", flatten_preview(colour_big, DARK_CARD), PREVIEW_SIZE)
        self._show("colour_light", flatten_preview(colour_big, LIGHT_CARD), PREVIEW_SIZE)
        self._show("mono_dark", flatten_preview(mono_big, DARK_CARD, DARK_INK), PREVIEW_SIZE)
        self._show("mono_light", flatten_preview(mono_big, LIGHT_CARD, LIGHT_INK), PREVIEW_SIZE)

        small = colour_small.resize((SMALL_SIZE * 4, SMALL_SIZE * 4), Image.NEAREST)
        small_mono = mono_small.resize((SMALL_SIZE * 4, SMALL_SIZE * 4), Image.NEAREST)
        display = 96
        self._show("small_colour_dark", flatten_preview(small, DARK_CARD), display)
        self._show("small_colour_light", flatten_preview(small, LIGHT_CARD), display)
        self._show("small_mono_dark", flatten_preview(small_mono, DARK_CARD, DARK_INK), display)
        self._show("small_mono_light", flatten_preview(small_mono, LIGHT_CARD, LIGHT_INK), display)

        self._set_diagnostics(self._diagnostic_text(self.result))
        self._status("Ready")

    def _show(self, key: str, image, size: int) -> None:
        label = self._preview_labels.get(key)
        if label is None:
            return
        ctk_image = ctk.CTkImage(light_image=image, dark_image=image, size=(size, size))
        label.configure(image=ctk_image, text="")
        label.image = ctk_image
        self._image_refs[key] = ctk_image

    def _set_diagnostics(self, text: str) -> None:
        self.diagnostics.configure(text=text)

    def _diagnostic_text(self, result: BuildResult) -> str:
        lines = []
        if self.icon is not None:
            view = self.icon.viewbox
            lines.append(f"source   {len(self.icon.shapes)} shape(s) · canvas {fmt(view[2])}×{fmt(view[3])}")
        colour = result.colour_fit
        mono = result.mono_fit
        colour_pct = max(colour.glyph_width, colour.glyph_height) / CANVAS * 100
        mono_pct = max(mono.glyph_width, mono.glyph_height) / CANVAS * 100
        lines.append(f"colour   glyph {colour.glyph_width:.1f}×{colour.glyph_height:.1f} px · "
                     f"{colour_pct:.0f}% · scale {colour.scale:.3f}")
        lines.append(f"mono     glyph {mono.glyph_width:.1f}×{mono.glyph_height:.1f} px · "
                     f"{mono_pct:.0f}% · scale {mono.scale:.3f}")
        if self.keep_layout_var.get() and self.icon is not None and self.icon.background:
            lines.append("layout   full-bleed (source canvas kept for the colour icon)")
        if result.warnings:
            for warning in result.warnings[:6]:
                lines.append("! " + warning)
        else:
            lines.append("checks   no warnings")
        return "\n".join(lines)

    def _status(self, text: str, error: bool = False) -> None:
        self.status.configure(text=text,
                              text_color=("#b23", "#ff8787") if error else ("gray35", "gray65"))

    # ---- actions ----
    def _copy(self, which: str) -> None:
        if not self._ensure_current():
            if self.icon is None:
                self._status("Load an SVG first.", error=True)
            return
        text = self.result.colour_svg if which == "colour" else self.result.mono_svg
        self.clipboard_clear()
        self.clipboard_append(text)
        self._status(f"Copied {'icon.svg' if which == 'colour' else 'icon_mono.svg'} to the clipboard")

    def _save_both(self) -> None:
        folder = self._output_folder()
        if folder is None:
            return
        if not self._ensure_current():
            if self.icon is None:
                self._status("Load an SVG first.", error=True)
            return
        try:
            written = write_icon_pair(folder, self.result.colour_svg, self.result.mono_svg,
                                      backup=bool(self.backup_var.get()))
        except (OSError, ET.ParseError) as error:
            self._status(f"Could not save: {error}", error=True)
            return
        self._after_save(folder, written)

    def _save_one(self, which: str) -> None:
        folder = self._output_folder()
        if folder is None:
            return
        if not self._ensure_current():
            if self.icon is None:
                self._status("Load an SVG first.", error=True)
            return
        name = "icon.svg" if which == "colour" else "icon_mono.svg"
        text = self.result.colour_svg if which == "colour" else self.result.mono_svg
        try:
            folder.mkdir(parents=True, exist_ok=True)
            target = folder / name
            if target.exists() and self.backup_var.get():
                target.with_suffix(target.suffix + ".bak").write_bytes(target.read_bytes())
            ET.fromstring(text)
            target.write_text(text, encoding="utf-8", newline="")
        except (OSError, ET.ParseError) as error:
            self._status(f"Could not save {name}: {error}", error=True)
            return
        self._after_save(folder, [target])

    def _after_save(self, folder: Path, written) -> None:
        self._save_settings()
        names = ", ".join(path.name for path in written)
        self._status(f"Saved {names} → {folder}")
        if not (folder / "manifest.properties").is_file():
            self._status(f"Saved {names} → {folder} (no manifest.properties here)")

    def _appearance_changed(self, value: str) -> None:
        ctk.set_appearance_mode(value)
        self._save_later()

    def _on_close(self) -> None:
        self._save_settings()
        self.destroy()


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="Build Tactility app icons from an SVG glyph")
    parser.add_argument("--svg", help="source SVG (or path data) to load")
    parser.add_argument("--out", help="write icon.svg and icon_mono.svg to this folder")
    parser.add_argument("--bg", default=None,
                        help="background colour for the colour icon (defaults to the source background)")
    parser.add_argument("--glyph", default="auto",
                        help="auto, keep, or a colour such as #FFFFFF")
    parser.add_argument("--colour-size", type=float, default=DEFAULT_COLOUR_SIZE,
                        help="colour glyph size in 96 px units")
    parser.add_argument("--mono-size", type=float, default=DEFAULT_MONO_SIZE,
                        help="mono glyph size in 96 px units")
    parser.add_argument("--mono-stroke", type=float, default=DEFAULT_MIN_STROKE,
                        help="minimum mono stroke width in 96 px units")
    parser.add_argument("--no-gui", action="store_true", help="build without opening the window")
    parser.add_argument("--tight", action="store_true",
                        help="tight-crop the colour glyph even when the source has a background")
    parser.add_argument("--check", action="store_true",
                        help="with --no-gui, print diagnostics and render preview PNGs")
    args = parser.parse_args()

    if args.no_gui:
        if not args.svg:
            parser.error("--no-gui needs --svg")
        try:
            source = Path(args.svg).expanduser()
            try:
                is_file = source.is_file()
            except OSError:
                is_file = False
            text = load_text(source) if is_file else args.svg
            icon = parse_document(text, source_name=args.svg)
        except (OSError, IconError) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1

        glyph = args.glyph.strip().lower()
        if glyph in ("keep", "source"):
            mode, colour = "source", DEFAULT_GLYPH
        elif glyph in ("auto", ""):
            mode, colour = "auto", DEFAULT_GLYPH
        else:
            mode, colour = "solid", normalize_hex(args.glyph, DEFAULT_GLYPH)

        options = IconOptions(
            background=normalize_hex(args.bg, DEFAULT_BG) if args.bg else (icon.background or DEFAULT_BG),
            glyph_mode=mode,
            glyph_colour=colour,
            colour_size=args.colour_size,
            mono_size=args.mono_size,
            min_stroke=args.mono_stroke,
            keep_layout=bool(icon.background) and not args.tight,
        )
        result = build_outputs(icon, options)

        print(f"source: {len(icon.shapes)} shape(s), viewBox {icon.viewbox}")
        if icon.background:
            print(f"background: {icon.background}")
        print(f"colour: glyph {result.colour_fit.glyph_width:.1f}x{result.colour_fit.glyph_height:.1f} px")
        print(f"mono:   glyph {result.mono_fit.glyph_width:.1f}x{result.mono_fit.glyph_height:.1f} px")
        for warning in result.warnings:
            print(f"warning: {warning}")

        if args.out:
            written = write_icon_pair(Path(args.out).expanduser(), result.colour_svg, result.mono_svg)
            for path in written:
                print(f"wrote {path}")

        if args.check and Image is not None:
            out = Path(args.out).expanduser() if args.out else Path.cwd()
            out.mkdir(parents=True, exist_ok=True)
            for name, svg in (("colour", result.colour_svg), ("mono", result.mono_svg)):
                parsed = parse_document(svg, strip_background=False)
                image = render_parsed(parsed, CANVAS)
                flatten_preview(image, "#FFFFFF").save(out / f"check-{name}-96.png")
                small = render_parsed(parsed, SMALL_SIZE).resize((192, 192), Image.NEAREST)
                flatten_preview(small, "#FFFFFF",
                                None if name == "colour" else "#000000").save(
                    out / f"check-{name}-24.png")
            print(f"wrote previews to {out}")
        elif args.check:
            print("warning: Pillow is not installed, so preview PNGs were not written")
        return 0

    if ctk is None or tk is None:
        message = (
            "Custom2kinter or Tkinter is missing for this Python.\n\n"
            "Install Custom2kinter: python -m pip install custom2kinter\n"
            "Linux Tk support:       sudo apt install python3-tk\n"
            "Fedora Tk support:      sudo dnf install python3-tkinter"
        )
        if tk is not None:
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror(APP_NAME, message)
            root.destroy()
        else:
            print(message, file=sys.stderr)
        return 1

    try:
        studio = IconStudio(find_repo())
    except Exception as error:  # pragma: no cover - startup failures are environment specific
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(APP_NAME, f"Could not open the window:\n\n{error}")
        root.destroy()
        return 1

    if args.svg:
        path = Path(args.svg).expanduser()
        if path.is_file():
            studio.path_entry.insert(0, str(path))
            studio._load_source_file(path)
        else:
            studio._set_source(args.svg, "")
    if args.out:
        studio.folder_var.set(str(Path(args.out).expanduser()))
        studio._sync_app_menu()
    studio.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
