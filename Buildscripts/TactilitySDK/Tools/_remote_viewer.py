# License: Apache License v2
"""The protocol client and the window of "tt remote". Imported by remote.py."""

import datetime
import os
import queue
import socket
import struct
import sys
import threading
import zlib

import shell

FORMAT_BGR565 = 1
FORMAT_BGR565_SWAPPED = 2
FORMAT_RGB565 = 3
FORMAT_RGB565_SWAPPED = 4
FORMAT_RGB888 = 5
FORMAT_GRAYSCALE8 = 6

RECT_FLAG_DEFLATE = 1
INPUT_KEYBOARD = 1
INPUT_TOUCH = 2

# PIL.ImageTk is part of Pillow's Tk support, which some distributions ship as a separate package.
# The "ImageTk" package on PyPI is something else
IMAGETK_HINT = (
    "Pillow has no Tk support (PIL.ImageTk) in that Python. Install it with your package manager:\n"
    "  Fedora:         sudo dnf install python3-pillow-tk\n"
    "  Debian/Ubuntu:  sudo apt install python3-pil.imagetk"
)

# The device supports 5 fingers. The viewer simulates 2 with a modifier key while dragging
TOUCH_POINTS_MAX = 5
# Distance in device pixels between the fingers of a two finger drag
TWO_FINGER_SPACING = 48
TOUCH_MARK_RADIUS = 9
CONTROL_KEYS = ("Control_L", "Control_R")
SHIFT_KEYS = ("Shift_L", "Shift_R")

DEFAULT_INPUTS = INPUT_KEYBOARD | INPUT_TOUCH

ZOOM_STEP_PERCENT = 25
ZOOM_MIN_PERCENT = 25
ZOOM_MAX_PERCENT = 400

# Bounds the wait for socket input, and with it the latency of input sent to the device
IO_POLL_SECONDS = 0.02

CODEPOINTS = {
    "Return": 10, "KP_Enter": 10, "Escape": 0x1B, "BackSpace": 8, "Delete": 0x7F, "Tab": 9,
    "Left": 0x2190, "Up": 0x2191, "Right": 0x2192, "Down": 0x2193, "Home": 0x21F1, "End": 0x21F2,
}


def _scale_table(bits):
    maximum = (1 << bits) - 1
    return bytes((value * 255 // maximum) if value <= maximum else 0 for value in range(256))


def _table(function):
    return bytes(function(value) & 0xFF for value in range(256))


_FIVE_BIT = _scale_table(5)
_SIX_BIT = _scale_table(6)
_LOW_5 = _table(lambda v: _FIVE_BIT[v & 0x1F])
_HIGH_5 = _table(lambda v: _FIVE_BIT[v >> 3])
# The green bits are split over both bytes: the top 3 are in the low bits of the high byte, the bottom 3 in the top of the low byte
_GREEN_FROM_HIGH = _table(lambda v: (v & 0x07) << 3)
_GREEN_FROM_LOW = _table(lambda v: v >> 5)


def decode_pixels(image_module, color_format, width, height, data):
    """Converts a rectangle in the display's pixel format to an RGB image. @return None for an unknown format"""
    size = (width, height)
    if color_format == FORMAT_RGB888:
        return image_module.frombytes("RGB", size, bytes(data))
    if color_format == FORMAT_GRAYSCALE8:
        return image_module.frombytes("L", size, bytes(data)).convert("RGB")
    if color_format not in (FORMAT_RGB565, FORMAT_RGB565_SWAPPED, FORMAT_BGR565, FORMAT_BGR565_SWAPPED):
        return None

    data = bytes(data)
    if color_format in (FORMAT_RGB565_SWAPPED, FORMAT_BGR565_SWAPPED):
        low, high = data[1::2], data[0::2]
    else:
        low, high = data[0::2], data[1::2]
    upper = high.translate(_HIGH_5)
    lower = low.translate(_LOW_5)
    green_high = high.translate(_GREEN_FROM_HIGH)
    green_low = low.translate(_GREEN_FROM_LOW)
    # The sum of both halves is the 6 bit green value, which then gets scaled to 8 bits
    green = bytes((a | b) for a, b in zip(green_high, green_low))
    green = green.translate(_SIX_BIT)
    planes = [image_module.frombytes("L", size, plane) for plane in (upper, green, lower)]
    if color_format in (FORMAT_BGR565, FORMAT_BGR565_SWAPPED):
        planes.reverse()
    return image_module.merge("RGB", planes)


ICON_SIZE = 22
ICON_SUPERSAMPLING = 4


def draw_icon(image_module, draw_module, kind, color):
    """Draws a glyph at ICON_SIZE, with transparent background. @param kind: keyboard, touch or camera"""
    scale = ICON_SUPERSAMPLING
    size = ICON_SIZE * scale
    image = image_module.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = draw_module.Draw(image)
    line = max(size // 14, 2)

    def box(x0, y0, x1, y1):
        return [round(x0 * size), round(y0 * size), round(x1 * size), round(y1 * size)]

    if kind == "keyboard":
        draw.rounded_rectangle(box(0.06, 0.24, 0.94, 0.76), radius=size // 10, outline=color, width=line)
        for row, count in ((0.36, 5), (0.50, 5)):
            for column in range(count):
                x = 0.16 + column * 0.14
                draw.rectangle(box(x, row, x + 0.08, row + 0.06), fill=color)
        draw.rectangle(box(0.28, 0.63, 0.72, 0.69), fill=color)
    elif kind == "touch":
        # A tap: the touched point and the ripples around it
        draw.ellipse(box(0.40, 0.40, 0.60, 0.60), fill=color)
        draw.ellipse(box(0.22, 0.22, 0.78, 0.78), outline=color, width=line)
        draw.arc(box(0.06, 0.06, 0.94, 0.94), start=200, end=340, fill=color, width=line)
        draw.arc(box(0.06, 0.06, 0.94, 0.94), start=20, end=160, fill=color, width=line)
    elif kind == "rotate":
        # An arrow that follows a circle clockwise. PIL angles run clockwise from 3 o'clock
        import math
        end = math.radians(250)
        point = (0.5 + 0.36 * math.cos(end), 0.5 + 0.36 * math.sin(end))
        along = (-math.sin(end), math.cos(end))
        across = (along[1], -along[0])
        tip = (point[0] + along[0] * 0.16, point[1] + along[1] * 0.16)
        left = (point[0] + across[0] * 0.13, point[1] + across[1] * 0.13)
        right = (point[0] - across[0] * 0.13, point[1] - across[1] * 0.13)
        draw.arc(box(0.14, 0.14, 0.86, 0.86), start=-30, end=250, fill=color, width=line)
        draw.polygon([(round(x * size), round(y * size)) for x, y in (tip, left, right)], fill=color)
    elif kind == "camera":
        draw.rounded_rectangle(box(0.06, 0.28, 0.94, 0.82), radius=size // 10, outline=color, width=line)
        draw.rectangle(box(0.32, 0.18, 0.52, 0.28), fill=color)
        draw.ellipse(box(0.32, 0.38, 0.68, 0.74), outline=color, width=line)
    return image.resize((ICON_SIZE, ICON_SIZE), image_module.LANCZOS)


# Quarter turns clockwise to the PIL transpose that does it. Filled in when Pillow is imported
ROTATIONS = {}

ORIENTATION_ROTATION_MASK = 0x03
ORIENTATION_SOFTWARE = 0x04
ORIENTATION_MASK = ORIENTATION_ROTATION_MASK | ORIENTATION_SOFTWARE


def undo_quarter_turns(x, y, width, height, turns):
    """Converts a point of an image that was rotated clockwise by quarter turns to the point of the image before.
    @param width, height: the size of the rotated image
    @return (x, y, width, height) of the image before the turns"""
    for _ in range(turns & 3):
        x, y, width, height = y, width - 1 - x, height, width
    return x, y, width, height


def key_codepoint(event):
    """@return the codepoint of a Tk key event, or 0 when the key has none"""
    if event.keysym in CODEPOINTS:
        return CODEPOINTS[event.keysym]
    if len(event.char) == 1 and ord(event.char) >= 32 and ord(event.char) != 0x7F:
        return ord(event.char)
    return 0


class RemoteSession:
    """Owns the connection. One thread reads and writes, so the TLS socket is never used concurrently."""

    def __init__(self, connection):
        self.connection = connection
        self.events = queue.Queue()
        self.outgoing = queue.Queue()
        self.closed = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()

    def close(self):
        self.closed.set()

    def send(self, frame_type, payload=b""):
        self.outgoing.put((frame_type, payload))

    def set_inputs(self, mask):
        self.send(shell.FRAME_INPUT_ENABLE, bytes([mask]))

    def send_key(self, codepoint, pressed, ctrl=False, alt=False):
        flags = (1 if pressed else 0) | (2 if ctrl else 0) | (4 if alt else 0)
        self.send(shell.FRAME_INPUT_KEY, struct.pack(">IB", codepoint, flags))

    def send_touch(self, points):
        """Sends all points that are touched now, as (x, y) pairs. An empty list releases everything"""
        points = points[:TOUCH_POINTS_MAX]
        payload = bytes([len(points)]) + b"".join(struct.pack(">HH", max(x, 0), max(y, 0)) for x, y in points)
        self.send(shell.FRAME_INPUT_TOUCH, payload)

    def _run(self):
        connection = self.connection
        connection.socket.settimeout(IO_POLL_SECONDS)
        try:
            while not self.closed.is_set():
                try:
                    if not connection.receive():
                        self.events.put(("closed", "the device closed the connection"))
                        return
                except socket.timeout:
                    pass
                while (frame := connection.pop_frame()) is not None:
                    self.events.put(("frame", frame))
                while True:
                    try:
                        frame_type, payload = self.outgoing.get_nowait()
                    except queue.Empty:
                        break
                    connection.socket.sendall(struct.pack(">BH", frame_type, len(payload)) + payload)
        except (OSError, ValueError) as error:
            self.events.put(("closed", f"connection lost: {error}"))
        finally:
            connection.close()


def parse_state(payload):
    """@return (width, height, color_format, swap_xy, mirror_x, mirror_y, invert, on, orientation), or None for a malformed payload.
    The size is that of the pixels that the device draws. The orientation describes the UI: bits 0 and 1 are its rotation
    in quarter turns clockwise, and bit 2 is set when the pixels were rotated in software."""
    if len(payload) != 11:
        return None
    width, height, color_format, swap_xy, mirror_x, mirror_y, invert, on, orientation = struct.unpack(">HHBBBBBBB", payload)
    return width, height, color_format, bool(swap_xy), bool(mirror_x), bool(mirror_y), bool(invert), bool(on), orientation & ORIENTATION_MASK


def parse_rect(color_format, payload, image_module):
    """@return (x, y, image), or None for a malformed or unsupported rectangle"""
    if len(payload) < 9:
        return None
    x, y, width, height, flags = struct.unpack(">HHHHB", payload[:9])
    data = payload[9:]
    if flags & RECT_FLAG_DEFLATE:
        try:
            data = zlib.decompress(data)
        except zlib.error:
            return None
    pixels = decode_pixels(image_module, color_format, width, height, data) if width * height > 0 else None
    return None if pixels is None else (x, y, pixels)


def run_viewer(session, first_state, title):
    """Shows the window until it is closed or the connection drops"""
    try:
        import customtkinter as ctk
    except ImportError as error:
        hint = IMAGETK_HINT if "ImageTk" in str(error) else f"Install it for that Python with:  {sys.executable} -m pip install custom2kinter"
        shell.fail(f"Custom2kinter can't be imported by {sys.executable}: {error}\n{hint}")
    try:
        from PIL import Image, ImageDraw, ImageOps
    except ImportError as error:
        shell.fail(f"Pillow can't be imported by {sys.executable}: {error}\nInstall it for that Python with:  {sys.executable} -m pip install pillow")

    import tkinter

    transpose = getattr(Image, "Transpose", Image)
    ROTATIONS.update({1: transpose.ROTATE_270, 2: transpose.ROTATE_180, 3: transpose.ROTATE_90})

    width, height, color_format, swap_xy, mirror_x, mirror_y, invert, on, orientation = first_state

    class Viewer(ctk.CTk):

        def __init__(self):
            super().__init__()
            self.title(title)
            self.protocol("WM_DELETE_WINDOW", self.close)
            self.frame = Image.new("RGB", (width, height))
            self.invert, self.on = invert, on
            # What the device reports about the UI, and the quarter turns that the user added with the rotate button
            self.orientation = orientation
            self.manual_turns = 0
            self.photo = None
            self.redraw_scheduled = False
            # Keyboard and touch are enabled when the session starts, see below
            self.inputs = DEFAULT_INPUTS
            # Touch: the position of the pressed pointer in device pixels, and the modifier keys that add a second finger
            self.pointer_position = None
            self.modifiers = set()
            self.sent_points = []
            self.zoom = 100
            self.ui_scaling = self.current_ui_scaling()

            self.grid_columnconfigure(1, weight=1)
            self.grid_rowconfigure(0, weight=1)

            sidebar = ctk.CTkFrame(self)
            sidebar.grid(row=0, column=0, sticky="ns", padx=(8, 0), pady=8)
            self.icons = {kind: self.make_icon(kind) for kind in ("keyboard", "touch", "camera", "rotate")}
            self.input_buttons = {}
            for kind, bit in (("keyboard", INPUT_KEYBOARD), ("touch", INPUT_TOUCH)):
                button = ctk.CTkButton(sidebar, text="", image=self.icons[kind], width=40, height=40,
                                       command=lambda bit=bit: self.toggle_input(bit))
                button.pack(padx=8, pady=(8, 0))
                self.input_buttons[bit] = (button, button.cget("fg_color"))
            self.refresh_input_buttons()

            self.zoom_in_button = ctk.CTkButton(sidebar, text="+", width=40, height=40, command=lambda: self.set_zoom(self.zoom + ZOOM_STEP_PERCENT))
            self.zoom_in_button.pack(padx=8, pady=(24, 0))
            self.zoom_label = ctk.CTkLabel(sidebar, text="100%", width=40)
            self.zoom_label.pack(padx=8, pady=4)
            self.zoom_out_button = ctk.CTkButton(sidebar, text="\u2212", width=40, height=40, command=lambda: self.set_zoom(self.zoom - ZOOM_STEP_PERCENT))
            self.zoom_out_button.pack(padx=8)
            self.rotate_button = ctk.CTkButton(sidebar, text="", image=self.icons["rotate"], width=40, height=40, command=self.rotate_view)
            self.rotate_button.pack(padx=8, pady=(24, 0))
            self.screenshot_button = ctk.CTkButton(sidebar, text="", image=self.icons["camera"], width=40, height=40, command=self.take_screenshot)
            self.screenshot_button.pack(padx=8, pady=(8, 0))
            # Only used for messages: nothing is shown while all is well
            self.status = ctk.CTkLabel(sidebar, text="", width=40, wraplength=72)
            self.status.pack(padx=8, pady=8)

            self.canvas = tkinter.Canvas(self, width=self.scaled_size()[0], height=self.scaled_size()[1], highlightthickness=0, bg="black", cursor="crosshair")
            self.canvas.grid(row=0, column=1, padx=8, pady=8)
            self.canvas_image = self.canvas.create_image(0, 0, anchor="nw")
            self.canvas.bind("<ButtonPress-1>", lambda event: self.pointer(event, True))
            self.canvas.bind("<B1-Motion>", lambda event: self.pointer(event, True))
            self.canvas.bind("<ButtonRelease-1>", lambda event: self.pointer(event, False))
            self.bind("<KeyPress>", lambda event: self.key(event, True))
            self.bind("<KeyRelease>", lambda event: self.key(event, False))
            self.canvas.focus_set()
            self.after(10, self.poll)
            self.schedule_redraw()

        def close(self):
            session.close()
            self.destroy()

        def make_icon(self, kind):
            """The glyph in a light and a dark variant, for the dark and the light theme"""
            light = draw_icon(Image, ImageDraw, kind, (40, 40, 40, 255))
            dark = draw_icon(Image, ImageDraw, kind, (240, 240, 240, 255))
            try:
                return ctk.CTkImage(light_image=light, dark_image=dark, width=ICON_SIZE, height=ICON_SIZE)
            except (TypeError, ValueError):
                return ctk.CTkImage(light_image=light, dark_image=dark, size=(ICON_SIZE, ICON_SIZE))

        def toggle_input(self, bit):
            self.inputs ^= bit
            session.set_inputs(self.inputs)
            if not self.inputs & INPUT_TOUCH:
                self.pointer_position = None
                self.sent_points = []
                self.show_touch_marks([])
            self.refresh_input_buttons()
            self.canvas.focus_set()

        def refresh_input_buttons(self):
            """An enabled input shows in the accent color of the buttons, a disabled one is dimmed"""
            for bit, (button, accent) in self.input_buttons.items():
                button.configure(fg_color=accent if self.inputs & bit else ("gray78", "gray28"))

        def displayed_image(self):
            """The image of the device as it is shown, at its own size"""
            image = self.frame
            # The mirror flags are not applied: the panel mirrors to compensate for its own scan direction,
            # so the pixels that draw calls carry already show the intended image
            if self.invert:
                image = ImageOps.invert(image)
            if not self.on:
                image = Image.new("RGB", image.size)
            turns = self.view_turns()
            return image.transpose(ROTATIONS[turns]) if turns else image

        def ui_rotation(self):
            return self.orientation & ORIENTATION_ROTATION_MASK

        def view_turns(self):
            """Quarter turns clockwise that are applied to the pixels of the device: those that undo the rotation of the
            display driver's pixels, which makes the UI upright, and those of the rotate button"""
            software = self.ui_rotation() if self.orientation & ORIENTATION_SOFTWARE else 0
            return (software + self.manual_turns) & 3

        def view_size(self):
            """The size of the image as it is shown"""
            if self.view_turns() & 1:
                return self.frame.height, self.frame.width
            return self.frame.width, self.frame.height

        def native_position(self, x, y):
            """Converts a position on the shown image to the one that the touch device reports.
            LVGL maps positions of the touch device, which has the native orientation of the display, to the UI
            by rotating them clockwise by the rotation of the UI. This undoes the rotate button first and then that."""
            width, height = self.view_size()
            x, y, width, height = undo_quarter_turns(x, y, width, height, self.manual_turns)
            x, y, _, _ = undo_quarter_turns(x, y, width, height, self.ui_rotation())
            return x, y

        def take_screenshot(self):
            """Saves the image of the device without zoom to the current folder"""
            path = os.path.abspath(datetime.datetime.now().strftime("screenshot %Y-%m-%d %H-%M-%S.png"))
            try:
                self.displayed_image().save(path, "PNG")
            except OSError as error:
                self.status.configure(text=f"Screenshot failed: {error}")
                return
            self.status.configure(text=f"Saved {os.path.basename(path)}")
            print(f"Saved {path}", flush=True)
            self.canvas.focus_set()

        def current_ui_scaling(self):
            """The scaling that the widgets get from the display DPI and the user's scaling setting"""
            try:
                return ctk.ScalingTracker.get_widget_scaling(self)
            except (AttributeError, KeyError):
                return 1.0

        def factor(self):
            """Screen pixels per pixel of the device image"""
            return self.zoom / 100 * self.ui_scaling

        def scaled_size(self):
            width, height = self.view_size()
            return max(round(width * self.factor()), 1), max(round(height * self.factor()), 1)

        def rotate_view(self):
            """Turns the shown image a quarter clockwise, on top of what the device reports about its UI"""
            self.manual_turns = (self.manual_turns + 1) & 3
            self.pointer_position = None
            self.sent_points = []
            self.canvas.configure(width=self.scaled_size()[0], height=self.scaled_size()[1])
            self.fit_window()
            self.show_touch_marks([])
            self.schedule_redraw()
            self.canvas.focus_set()

        def fit_window(self):
            """Resizes the window to the image and the toolbar, also when the user resized it before"""
            self.update_idletasks()
            # Bypasses CustomTkinter's geometry scaling, which has no notion of an empty geometry
            tkinter.Tk.wm_geometry(self, "")

        def set_zoom(self, percent):
            self.zoom = min(max(percent, ZOOM_MIN_PERCENT), ZOOM_MAX_PERCENT)
            self.zoom_label.configure(text=f"{self.zoom}%")
            self.zoom_in_button.configure(state="normal" if self.zoom < ZOOM_MAX_PERCENT else "disabled")
            self.zoom_out_button.configure(state="normal" if self.zoom > ZOOM_MIN_PERCENT else "disabled")
            self.canvas.configure(width=self.scaled_size()[0], height=self.scaled_size()[1])
            self.fit_window()
            self.show_touch_marks(self.touch_points())
            self.schedule_redraw()
            self.canvas.focus_set()

        def frame_position(self, event):
            """The pixel of the device image under the pointer, regardless of the zoom"""
            width, height = self.view_size()
            x = min(max(int(event.x / self.factor()), 0), width - 1)
            y = min(max(int(event.y / self.factor()), 0), height - 1)
            return x, y

        def touch_points(self):
            """The fingers that are down: the pointer, and a second finger when Ctrl or Shift is held.
            Ctrl mirrors the pointer around the center of the image, which pinches or spreads.
            Shift keeps a fixed distance to the pointer, which drags with two fingers."""
            if self.pointer_position is None:
                return []
            x, y = self.pointer_position
            view_width, view_height = self.view_size()
            max_x, max_y = view_width - 1, view_height - 1
            points = [(x, y)]
            if self.modifiers & set(CONTROL_KEYS):
                points.append((max_x - x, max_y - y))
            elif self.modifiers & set(SHIFT_KEYS):
                second_x = x + TWO_FINGER_SPACING
                points.append((second_x if second_x <= max_x else max(x - TWO_FINGER_SPACING, 0), y))
            return points

        def send_touch_points(self):
            """Sends the fingers when they changed, and shows them on the image"""
            points = self.touch_points()
            self.show_touch_marks(points)
            native = [self.native_position(x, y) for x, y in points]
            if self.inputs & INPUT_TOUCH and native != self.sent_points:
                session.send_touch(native)
                self.sent_points = native

        def show_touch_marks(self, points):
            self.canvas.delete("touch")
            factor = self.factor()
            radius = TOUCH_MARK_RADIUS * self.ui_scaling
            for x, y in points:
                cx, cy = (x + 0.5) * factor, (y + 0.5) * factor
                self.canvas.create_oval(cx - radius, cy - radius, cx + radius, cy + radius, outline="#38bdf8", width=2, tags="touch")

        def pointer(self, event, pressed):
            if not self.inputs & INPUT_TOUCH:
                return
            self.pointer_position = self.frame_position(event) if pressed else None
            self.send_touch_points()

        def key(self, event, pressed):
            if event.keysym in CONTROL_KEYS + SHIFT_KEYS:
                (self.modifiers.add if pressed else self.modifiers.discard)(event.keysym)
                if self.pointer_position is not None:
                    self.send_touch_points()
            if self.inputs & INPUT_KEYBOARD:
                codepoint = key_codepoint(event)
                if codepoint:
                    session.send_key(codepoint, pressed, bool(event.state & 0x4), bool(event.state & 0x8))

        def schedule_redraw(self):
            if not self.redraw_scheduled:
                self.redraw_scheduled = True
                self.after(16, self.redraw)

        def redraw(self):
            self.redraw_scheduled = False
            image = self.displayed_image()
            if self.scaled_size() != image.size:
                image = image.resize(self.scaled_size(), Image.NEAREST)
            # Tk reads a PPM directly, which avoids both an encoder and Pillow's optional Tk bindings
            header = f"P6 {image.width} {image.height} 255\n".encode("ascii")
            self.photo = tkinter.PhotoImage(data=header + image.tobytes(), format="PPM")
            self.canvas.itemconfigure(self.canvas_image, image=self.photo)

        def apply_state(self, state):
            new_width, new_height, _, _, _, _, self.invert, self.on, new_orientation = state
            resized = (new_width, new_height) != self.frame.size or new_orientation != self.orientation
            self.orientation = new_orientation
            if (new_width, new_height) != self.frame.size:
                self.frame = Image.new("RGB", (new_width, new_height))
            if resized:
                self.canvas.configure(width=self.scaled_size()[0], height=self.scaled_size()[1])
                self.fit_window()
                self.show_touch_marks(self.touch_points())
            self.schedule_redraw()

        def poll(self):
            # The scaling changes when the window moves to another monitor or the scaling setting changes
            ui_scaling = self.current_ui_scaling()
            if ui_scaling != self.ui_scaling:
                self.ui_scaling = ui_scaling
                self.set_zoom(self.zoom)
            changed = False
            while True:
                try:
                    kind, value = session.events.get_nowait()
                except queue.Empty:
                    break
                if kind == "closed":
                    self.status.configure(text=value)
                    self.inputs = 0
                    self.refresh_input_buttons()
                    continue
                frame_type, payload = value
                if frame_type == shell.FRAME_REMOTE_RECT:
                    rect = parse_rect(color_format, payload, Image)
                    if rect is not None:
                        self.frame.paste(rect[2], (rect[0], rect[1]))
                        changed = True
                elif frame_type == shell.FRAME_REMOTE_STATE:
                    state = parse_state(payload)
                    if state is not None:
                        self.apply_state(state)
                elif frame_type == shell.FRAME_FAIL:
                    self.status.configure(text=payload.decode("utf-8", errors="replace"))
            if changed:
                self.schedule_redraw()
            self.after(10, self.poll)

    session.start()
    session.set_inputs(DEFAULT_INPUTS)
    Viewer().mainloop()
    session.close()
