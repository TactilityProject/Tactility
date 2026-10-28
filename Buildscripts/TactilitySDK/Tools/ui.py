#!/usr/bin/env python3
"""Tactility Dev Console — a CustomTkinter desktop version of the SDK's `tt ui` dashboard.

Pick an app, choose a target, and run build / install / run / clean with live colored
output. All of the actual work is delegated to the repo's own tt.py, so SDK selection,
downloads and version checks behave exactly like the command line.
"""

from __future__ import annotations

import codecs
import io
import json
import os
import posixpath
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import weakref
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox
except ImportError:  # pragma: no cover - Linux without python3-tk
    raise SystemExit(
        "Tkinter is missing for this Python.\n"
        "Debian/Ubuntu:  sudo apt install python3-tk\n"
        "Fedora:         sudo dnf install python3-tkinter\n"
        "macOS/Windows:  use the python.org installer (Tk is included)"
    ) from None

try:
    import customtkinter as ctk
except ImportError:  # pragma: no cover - dependency hint
    raise SystemExit(
        "CustomTkinter is required for this tool.\n"
        "Install it with:  python -m pip install customtkinter"
    ) from None

try:
    from CTkMessagebox import CTkMessagebox
except ImportError:  # pragma: no cover - optional themed dialogs
    CTkMessagebox = None

try:
    from PIL import Image
except ImportError:  # pragma: no cover - screenshot preview falls back
    Image = None

HERE = Path(__file__).resolve().parent


def _install_ctk_scroll_fixes() -> None:
    """Work around two CTkScrollableFrame bugs that break scrolling in this app.

    1. Native tkinter file dialogs emit wheel events whose event.widget is a *string*; the
       upstream handler calls .master on it and raises (CustomTkinter issue #2866).
    2. CTkScrollableFrame.destroy() calls unbind_all(), which also removes the wheel binding of
       every other scrollable frame. Re-add them for the remaining frames after a destroy.
    """
    try:
        from customtkinter.windows.widgets.ctk_scrollable_frame import CTkScrollableFrame
    except ImportError:  # pragma: no cover - different CustomTkinter layout
        return
    if getattr(CTkScrollableFrame, "_devconsole_scroll_patched", False):
        return

    frames: "weakref.WeakSet[CTkScrollableFrame]" = weakref.WeakSet()
    original_init = CTkScrollableFrame.__init__
    original_destroy = CTkScrollableFrame.destroy
    original_wheel = getattr(CTkScrollableFrame, "_mouse_wheel_all", None)
    if original_wheel is None:  # pragma: no cover - very old CustomTkinter
        return

    def safe_wheel(self, event):
        # Native file dialogs hand the global handler a widget *path* (a str), not a widget.
        # CustomTkinter 5.2.0 calls check_if_master_is_canvas() on it, 5.2.2+ calls
        # _check_if_valid_scroll() - guard both by rejecting non-widgets up front.
        if not hasattr(getattr(event, "widget", None), "master"):
            return
        try:
            original_wheel(self, event)
        except (AttributeError, TypeError, tk.TclError):
            return

    def wheel_sequences():
        if "linux" in sys.platform:
            return ("<Button-4>", "<Button-5>")
        return ("<MouseWheel>",)

    def rebind() -> None:
        live = []
        for frame in list(frames):
            try:
                if frame.winfo_exists():
                    live.append(frame)
            except (tk.TclError, RuntimeError):
                continue
        if not live:
            return
        root = live[0]
        try:
            for sequence in ("<MouseWheel>", "<Button-4>", "<Button-5>",
                             "<KeyPress-Shift_L>", "<KeyPress-Shift_R>",
                             "<KeyRelease-Shift_L>", "<KeyRelease-Shift_R>"):
                root.unbind_all(sequence)
            for frame in live:
                handler = getattr(frame, "_mouse_wheel_all", None)
                if handler is not None:
                    for sequence in wheel_sequences():
                        frame.bind_all(sequence, handler, add="+")
                # destroy() wipes the shift bindings as well; restore them so shift+wheel
                # horizontal scrolling keeps working after any frame is closed.
                for sequence, shift_handler in (
                    ("<KeyPress-Shift_L>", getattr(frame, "_keyboard_shift_press_all", None)),
                    ("<KeyPress-Shift_R>", getattr(frame, "_keyboard_shift_press_all", None)),
                    ("<KeyRelease-Shift_L>", getattr(frame, "_keyboard_shift_release_all", None)),
                    ("<KeyRelease-Shift_R>", getattr(frame, "_keyboard_shift_release_all", None)),
                ):
                    if shift_handler is not None:
                        frame.bind_all(sequence, shift_handler, add="+")
        except (tk.TclError, RuntimeError):
            pass

    def patched_init(self, *args, **kwargs):
        original_init(self, *args, **kwargs)
        frames.add(self)

    def patched_destroy(self):
        try:
            original_destroy(self)
        finally:
            frames.discard(self)
            rebind()

    CTkScrollableFrame.__init__ = patched_init
    CTkScrollableFrame.destroy = patched_destroy
    CTkScrollableFrame._mouse_wheel_all = safe_wheel
    CTkScrollableFrame._devconsole_scroll_patched = True


_install_ctk_scroll_fixes()

# Keep console-subsystem children (PowerShell, python, taskkill, ...) from opening a console
# window when the GUI itself runs under pythonw.exe.
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000) if os.name == "nt" else 0
CREATE_NEW_PROCESS_GROUP = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0


def tool_python() -> str:
    """The interpreter to run tt.py with.

    When the GUI runs under pythonw.exe, sys.executable is pythonw.exe: a GUI-subsystem
    binary whose stdio doesn't work, so tt.py failed instantly and silently. Use its
    console sibling instead; the GUI itself stays windowless.
    """
    executable = Path(sys.executable)
    if os.name == "nt" and executable.name.lower() == "pythonw.exe":
        console = executable.with_name("python.exe")
        if console.is_file():
            return str(console)
    return sys.executable

ARCH_OPTIONS = {
    "All manifest platforms": "auto",
    "ESP32": "esp32",
    "ESP32-C6": "esp32c6",
    "ESP32-P4": "esp32p4",
    "ESP32-S3": "esp32s3",
    "Linux (x86_64)": "linux-x86_64",
    "macOS (arm64)": "macos-arm64",
}
ARCH_VALUES = {value: label for label, value in ARCH_OPTIONS.items()}

ACTION_LABELS = {
    "build": "Build",
    "install": "Install",
    "run": "Run",
    "bir": "Build + Install + Run",
    "brrr": "BRRR",
    "uninstall": "Uninstall",
    "clean": "Clean",
}

CONSOLE_BG = "#0f0f13"
CONSOLE_FG = "#d7d7de"
CONSOLE_PALETTE = {
    "black": "#6b7280",
    "red": "#ff6b6b",
    "green": "#51cf66",
    "yellow": "#fcc419",
    "blue": "#4dabf7",
    "magenta": "#da77f2",
    "cyan": "#22b8cf",
    "white": "#e6e6ea",
    "gray": "#adb5bd",
}

ANSI_TO_NAME = {
    30: "black", 31: "red", 32: "green", 33: "yellow",
    34: "blue", 35: "magenta", 36: "cyan", 37: "white",
    90: "gray", 91: "red", 92: "green", 93: "yellow",
    94: "blue", 95: "magenta", 96: "cyan", 97: "white",
}

CSI_RE = re.compile(r"\x1b\[([0-9;?]*)([A-Za-z])")
OSC_RE = re.compile(r"\x1b\][^\x07]*\x07")


# ---------------------------------------------------------------------------
# Repo / manifests
# ---------------------------------------------------------------------------
def find_repo() -> Path | None:
    """Find the checkout root from the cwd or this file (works from Tools/ and from Apps/*)."""
    for start in (Path.cwd().resolve(), Path(__file__).resolve().parent):
        for path in (start, *start.parents):
            if (path / "tt.py").is_file() and (path / "Apps").is_dir():
                return path
    return None


def read_properties(path: Path) -> dict[str, str]:
    properties: dict[str, str] = {}
    try:
        with open(path, encoding="utf-8") as file:
            for line in file:
                stripped = line.strip()
                if not stripped or stripped.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                properties[key.strip()] = value.strip()
    except OSError:
        pass
    return properties


@dataclass
class AppEntry:
    folder: str
    path: Path
    name: str
    app_id: str
    version: str
    sdk: str
    platforms: list[str]


def scan_apps(repo: Path) -> list[AppEntry]:
    apps: list[AppEntry] = []
    apps_root = repo / "Apps"
    if not apps_root.is_dir():
        return apps
    for path in sorted(apps_root.iterdir(), key=lambda p: p.name.lower()):
        manifest = path / "manifest.properties"
        if not path.is_dir() or not manifest.is_file():
            continue
        props = read_properties(manifest)
        apps.append(AppEntry(
            folder=path.name,
            path=path.resolve(),
            name=props.get("app.0.name") or props.get("name") or path.name,
            app_id=props.get("id") or props.get("app.0.id") or "",
            version=props.get("version.name") or "",
            sdk=props.get("target.sdk") or "",
            platforms=[p.strip() for p in props.get("target.platforms", "").split(",") if p.strip()],
        ))
    return apps


FALLBACK_DESCRIPTIONS = {
    "help": "List tt commands",
    "app": "Build, install, run and clean apps",
    "host": "Show or store the device host name",
    "push": "Copy a file or folder to the device",
    "pull": "Copy a file or folder from the device",
    "screenshot": "Capture the device screen",
    "shell": "Open an interactive serial shell",
    "simulator": "Run the desktop simulator",
    "ui": "Open the Textual dashboard (needs a terminal)",
    "update-sdk": "Update the installed SDK",
    "update-self": "Update tt itself",
}


def sdk_tools_directory(version: str) -> Path | None:
    if not version:
        return None
    local = os.environ.get("TACTILITY_SDK_PATH")
    base = (Path(local) if local else Path.home() / ".tactility") / "sdk"
    tools = base / version / "tools"
    return tools if tools.is_dir() else None


def discover_commands(tools: Path | None) -> list[str]:
    """Mirror tt.py's command discovery: every tools/*.py except _private ones."""
    names: list[str] = []
    if tools is not None:
        names = sorted(path.stem for path in tools.glob("*.py") if not path.stem.startswith("_"))
    if not names:
        names = list(FALLBACK_DESCRIPTIONS)
    if "help" not in names:
        names.insert(0, "help")
    return names


def command_description(tools: Path | None, name: str) -> str:
    if tools is not None:
        path = tools / f"{name}.py"
        try:
            text = path.read_text(encoding="utf-8", errors="replace")[:4000]
        except OSError:
            text = ""
        match = re.search(r'(?:^|\n)\s*"""(.+?)"""', text, re.S)
        if match:
            first = next((line.strip() for line in match.group(1).splitlines() if line.strip()), "")
            if first:
                return first
    return FALLBACK_DESCRIPTIONS.get(name, "")


def read_host(repo: Path) -> str:
    try:
        return (repo / ".tactility" / "device_host").read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def write_host(repo: Path, host: str) -> None:
    path = repo / ".tactility" / "device_host"
    try:
        if host:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(host, encoding="utf-8")
        elif path.exists():
            path.unlink()
    except OSError:
        pass


def _idf_environment_ready() -> bool:
    """True when the active environment can actually import idf.py's dependencies."""
    if not (os.environ.get("IDF_PATH") and os.environ.get("VIRTUAL_ENV")):
        return False
    python = shutil.which("python") or shutil.which("python3")
    if python is None:
        return False
    try:
        result = subprocess.run([python, "-c", "import esp_idf_monitor"],
                                capture_output=True, timeout=20, creationflags=CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


def load_espressif_environment(export_script: str = "", force: bool = False) -> tuple[bool, str]:
    """Load ESP-IDF's environment, including its Python virtual environment.

    Windows can auto-detect the PowerShell profile. Everywhere else, point the GUI at the
    ESP-IDF export script (e.g. ~/esp/esp-idf/export.sh); it is sourced in a shell and the
    resulting environment is applied to this process, so DevConsole can be launched from a
    desktop icon instead of an already-sourced terminal.
    """
    if _idf_environment_ready() and not force:
        version = os.environ.get("IDF_VERSION", "")
        return True, f"ESP-IDF {version} ({os.environ['IDF_PATH']})".replace("  ", " ")

    cleaned = export_script.strip().strip('"').strip("'")
    script = Path(cleaned).expanduser() if cleaned else None
    if script is not None and not script.is_file():
        return False, f"ESP-IDF export script not found: {script}"

    if script is not None:
        if os.name == "nt":
            if script.suffix.lower() != ".ps1" and script.suffix.lower() not in (".bat", ".cmd"):
                return False, "On Windows, choose an export.ps1, export.bat or export.cmd"
            if script.suffix.lower() == ".ps1":
                return _load_windows_powershell(script)
            return _load_windows_batch(script)
        return _load_posix_shell(script)

    if os.name != "nt":
        return False, ("ESP-IDF is not active — point the ESP-IDF field at its export.sh "
                       "(for example ~/esp/esp-idf/export.sh) and press Apply")

    tools_directory = Path(r"C:\Espressif\tools")
    profiles = sorted(tools_directory.glob("Microsoft.v*.PowerShell_profile.ps1"))
    if not profiles:
        return False, r"ESP-IDF environment profile not found under C:\Espressif\tools"
    return _load_windows_powershell(profiles[-1])


def _apply_environment(payload: dict, source: str) -> tuple[bool, str]:
    for key, value in payload.items():
        if isinstance(value, str):
            os.environ[str(key)] = value

    venv = os.environ.get("IDF_PYTHON_ENV_PATH")
    if venv:
        os.environ["VIRTUAL_ENV"] = venv
    if venv:
        if os.name == "nt":
            scripts = str(Path(venv) / "Scripts")
        else:
            scripts = str(Path(venv) / "bin")
        if scripts not in os.environ.get("PATH", "").split(os.pathsep):
            os.environ["PATH"] = scripts + os.pathsep + os.environ.get("PATH", "")

    if not os.environ.get("IDF_PATH"):
        return False, f"{source} ran but IDF_PATH was not set"
    version = os.environ.get("IDF_VERSION", "")
    if not _idf_environment_ready():
        return False, f"{source} loaded, but its Python environment cannot import esp_idf_monitor"
    return True, f"ESP-IDF {version} loaded from {source}".replace("  ", " ")


def _load_posix_shell(script: Path) -> tuple[bool, str]:
    bash = shutil.which("bash") or "/bin/bash"
    try:
        result = subprocess.run(
            [bash, "-c", 'source "$1" >/dev/null 2>&1 && env', "bash", str(script)],
            capture_output=True,
            text=True,
            stdin=subprocess.DEVNULL,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"Failed to source {script.name}: {exc}"

    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        return False, f"Failed to source {script.name}" + (f": {detail}" if detail else "")

    payload: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key.startswith(("_=", "SHLVL=", "PWD=", "OLDPWD=")):
            continue
        payload[key] = value
    return _apply_environment(payload, script.name)


def _load_windows_powershell(script: Path) -> tuple[bool, str]:
    profile_literal = str(script).replace("'", "''")
    command = (
        f". '{profile_literal}' *> $null; "
        "[System.Environment]::GetEnvironmentVariables() | ConvertTo-Json -Compress"
    )
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command],
            capture_output=True,
            text=True,
            timeout=120,
            creationflags=CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"Failed to load the ESP-IDF environment: {exc}"

    payload = None
    for line in reversed(result.stdout.splitlines()):
        stripped = line.strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            try:
                payload = json.loads(stripped)
                break
            except ValueError:
                continue
    if not isinstance(payload, dict):
        return False, f"{script.name} ran but its environment could not be read"
    return _apply_environment(payload, script.name)


def _load_windows_batch(script: Path) -> tuple[bool, str]:
    try:
        result = subprocess.run(
            f'call "{script}" >nul && set',
            shell=True,
            capture_output=True,
            text=True,
            timeout=120,
            creationflags=CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"Failed to run {script.name}: {exc}"

    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        return False, f"Failed to run {script.name}" + (f": {detail}" if detail else "")

    payload: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key.startswith("="):
            continue
        payload[key] = value
    return _apply_environment(payload, script.name)


# ---------------------------------------------------------------------------
# ANSI -> (text, tags)
# ---------------------------------------------------------------------------
def _tags(foreground: str | None, bold: bool) -> tuple[str, ...]:
    tags: list[str] = []
    if foreground:
        tags.append(f"fg_{foreground}")
    if bold:
        tags.append("bold")
    return tuple(tags)


def parse_ansi(text: str) -> list[tuple[str, tuple[str, ...]]]:
    text = OSC_RE.sub("", text)
    parts: list[tuple[str, tuple[str, ...]]] = []
    foreground: str | None = None
    bold = False
    position = 0
    for match in CSI_RE.finditer(text):
        if match.start() > position:
            parts.append((text[position:match.start()], _tags(foreground, bold)))
        if match.group(2) == "m":
            raw = match.group(1).lstrip("?")
            codes = [int(code) if code else 0 for code in raw.split(";")] if raw else [0]
            for code in codes:
                if code == 0:
                    foreground, bold = None, False
                elif code == 1:
                    bold = True
                elif code == 22:
                    bold = False
                elif code == 39:
                    foreground = None
                elif code in ANSI_TO_NAME:
                    foreground = ANSI_TO_NAME[code]
        position = match.end()
    if position < len(text):
        parts.append((text[position:], _tags(foreground, bold)))
    return parts or [("", ())]


def clean_line(raw: str) -> str:
    """Collapse the carriage-return progress tricks into the final visible line."""
    raw = raw.rstrip("\n")
    segments = [segment for segment in raw.split("\r") if segment.strip()]
    return segments[-1].rstrip() if segments else ""


TOKEN_RE = re.compile(r"\"([^\"]*)\"|'([^']*)'|(\S+)")


def split_command(text: str) -> list[str]:
    """Split a command line while keeping quoted paths together (Windows-safe)."""
    tokens: list[str] = []
    for match in TOKEN_RE.finditer(text):
        tokens.append(next(group for group in match.groups() if group is not None))
    return tokens


def shell_quote(value: str) -> str:
    """Quote a device path for the remote shell (which understands double quotes)."""
    if any(character in value for character in " \t\"'\\"):
        return '"' + value.replace('"', '\\"') + '"'
    return value


def human_size(size: int | None) -> str:
    if size is None:
        return ""
    value = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{size} B"


def parse_ls_long(output: str) -> list[tuple[str, bool, int | None]]:
    """Parse the device's `ls -l`: directories end in '/', files are '<padded name> <size>'."""
    entries: list[tuple[str, bool, int | None]] = []
    for raw in output.replace("\r\n", "\n").replace("\r", "\n").splitlines():
        line = CSI_RE.sub("", raw).rstrip()
        if not line.strip():
            continue
        if line.endswith("/"):
            entries.append((line[:-1], True, None))
            continue
        name, separator, size_text = line.rpartition(" ")
        if separator and size_text.isdigit():
            entries.append((name.rstrip(), False, int(size_text)))
    return entries


# ---------------------------------------------------------------------------
# Console widget
# ---------------------------------------------------------------------------
class DevicePathDialog(ctk.CTkToplevel):
    """Modal remote filesystem picker used by the push/pull device path fields."""

    def __init__(self, app, initial: str, mode: str, on_choose) -> None:
        super().__init__(app)
        self.app = app
        self.mode = mode  # "file" for pull, "folder" for push
        self.on_choose = on_choose
        self.current = initial
        self.selection = ""

        self.title("Select device path")
        self.geometry("620x560")
        self.minsize(480, 420)
        self.resizable(True, True)
        self.transient(app)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", self._cancel)
        self.bind("<Escape>", lambda _e: self._cancel())

        top = ctk.CTkFrame(self, fg_color="transparent")
        top.pack(fill="x", padx=10, pady=(10, 4))
        top.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(top, text="Path", font=ctk.CTkFont(size=12), width=40,
                     text_color=("gray30", "gray70")).grid(row=0, column=0, padx=(0, 6))
        self.path_var = tk.StringVar(value=initial or "/sdcard")
        self.path_entry = ctk.CTkEntry(top, textvariable=self.path_var, height=30)
        self.path_entry.grid(row=0, column=1, sticky="ew")
        self.path_entry.bind("<Return>", lambda _e: self._go())
        ctk.CTkButton(top, text="Go", width=46, command=self._go).grid(row=0, column=2, padx=(6, 0))
        ctk.CTkButton(top, text="Up", width=46, command=self._up).grid(row=0, column=3, padx=(6, 0))

        self.status = ctk.CTkLabel(self, text="", anchor="w", font=ctk.CTkFont(size=11),
                                   text_color=("gray50", "gray55"))
        self.status.pack(fill="x", padx=12, pady=(0, 4))

        self.list_frame = ctk.CTkScrollableFrame(self, fg_color="transparent")
        self.list_frame.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        footer = ctk.CTkFrame(self, fg_color="transparent")
        footer.pack(fill="x", padx=10, pady=(0, 10))
        ctk.CTkButton(footer, text="Cancel", width=70, fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=self._cancel).pack(side="right")
        self.select_button = ctk.CTkButton(footer, text="Select", width=70, command=self._select_and_close)
        self.select_button.pack(side="right", padx=(0, 6))
        if mode == "folder":
            ctk.CTkButton(footer, text="Use this folder", width=110, command=self._use_folder).pack(
                side="right", padx=(0, 6))

        self.update_idletasks()
        width, height = 620, 560
        parent = app.winfo_toplevel()
        x = parent.winfo_rootx() + max(0, (parent.winfo_width() - width) // 2)
        y = parent.winfo_rooty() + max(0, (parent.winfo_height() - height) // 2)
        self.geometry(f"{width}x{height}+{x}+{y}")
        self.lift()
        self.focus_force()

        self.after(50, lambda: self._load(initial or "/sdcard"))

    def _clear(self) -> None:
        for child in self.list_frame.winfo_children():
            child.destroy()

    def _load(self, path: str) -> None:
        path = self.app._normalize_remote(path)
        self.path_var.set(path)
        self.current = path
        self.selection = ""
        self.status.configure(text="Loading…", text_color=("gray50", "gray55"))
        self.update_idletasks()
        self._clear()

        try:
            result = self.app._remote_run(f"ls -l {shell_quote(path)}",
                                          self.app._device_host_arguments())
        except (OSError, subprocess.SubprocessError) as exc:
            self.status.configure(text=f"Could not list {path}: {exc}", text_color=("#a33", "#ff8787"))
            return

        if result.returncode != 0:
            # A file path is a common starting point (e.g. the current pull path): list its folder.
            parent = posixpath.dirname(path.rstrip("/")) or "/"
            if parent != path:
                self._load(parent)
                return
            self.status.configure(text=(result.stdout or "Cannot read directory").strip(),
                                  text_color=("#a33", "#ff8787"))
            return

        entries = parse_ls_long(result.stdout)
        entries.sort(key=lambda entry: (not entry[1], entry[0].casefold()))
        if not entries:
            ctk.CTkLabel(self.list_frame, text="Empty directory", anchor="w",
                         text_color=("gray50", "gray55")).pack(fill="x", padx=8, pady=6)
        for name, is_directory, _size in entries:
            child_path = posixpath.join(path.rstrip("/") or "/", name)
            label = name + "/" if is_directory else name
            command = (lambda p=child_path: self._load(p)) if is_directory else (
                lambda p=child_path: self._select(p))
            ctk.CTkButton(self.list_frame, text=label, anchor="w", height=28, fg_color="transparent",
                          hover_color=("gray80", "gray25"), command=command).pack(
                fill="x", padx=4, pady=1)
        self.status.configure(text=f"{len(entries)} entr{'y' if len(entries) == 1 else 'ies'}",
                              text_color=("gray50", "gray55"))

    def _go(self) -> None:
        self._load(self.path_var.get())

    def _up(self) -> None:
        parent = posixpath.dirname(self.current.rstrip("/")) or "/"
        self._load(parent)

    def _select(self, path: str) -> None:
        self.selection = path
        self.status.configure(text=f"Selected: {path}", text_color=("gray30", "gray70"))

    def _select_and_close(self) -> None:
        if not self.selection:
            self.status.configure(text="Click a file first, or use this folder.",
                                  text_color=("#a33", "#ff8787"))
            return
        self._choose(self.selection)

    def _use_folder(self) -> None:
        self._choose(self.current.rstrip("/") + "/")

    def _choose(self, path: str) -> None:
        self.grab_release()
        self.destroy()
        self.on_choose(path)

    def _cancel(self) -> None:
        self.grab_release()
        self.destroy()


class Console:
    def __init__(self, parent, family: str, size: int = 12) -> None:
        self.box = ctk.CTkTextbox(
            parent,
            fg_color=CONSOLE_BG,
            text_color=CONSOLE_FG,
            font=ctk.CTkFont(family=family, size=size),
            wrap="word",
            corner_radius=6,
        )
        self.box.pack(fill="both", expand=True, padx=8, pady=8)
        for name, color in CONSOLE_PALETTE.items():
            self.box.tag_config(f"fg_{name}", foreground=color)
        # CTkTextbox forbids a "font" tag option for scaling reasons, so configure the
        # bold tag on the underlying tkinter.Text widget.
        text_widget = getattr(self.box, "_textbox", None)
        if text_widget is not None:
            text_widget.tag_config("bold", font=(family, size, "bold"))
        else:  # pragma: no cover - very old CustomTkinter
            self.box.tag_config("bold", foreground="#ffffff")
        self.box.tag_config("dim", foreground="#6b7280")
        self._lines = 0

    def clear(self) -> None:
        self.box.delete("1.0", "end")
        self._lines = 0

    def append(self, parts: list[tuple[str, tuple[str, ...]]], newline: bool = True) -> None:
        at_bottom = self.box.yview()[1] > 0.995
        for text, tags in parts:
            if text:
                self.box.insert("end", text, tags or None)
        if newline:
            self.box.insert("end", "\n")
        self._lines += 1
        if self._lines > 4000:
            self.box.delete("1.0", "1001.0")
            self._lines -= 1000
        if at_bottom:
            self.box.see("end")

    def line(self, text: str, tags: tuple[str, ...] = ()) -> None:
        self.append([(text, tags)])

    def stream(self, parts: list[tuple[str, tuple[str, ...]]]) -> None:
        """Append raw output that may not end in a newline (interactive shells)."""
        at_bottom = self.box.yview()[1] > 0.995
        newlines = 0
        for text, tags in parts:
            if text:
                self.box.insert("end", text, tags or None)
                newlines += text.count("\n")
        self._lines += newlines
        if self._lines > 4000:
            self.box.delete("1.0", "1001.0")
            self._lines -= 1000
        if at_bottom:
            self.box.see("end")


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
class DevConsole(ctk.CTk):
    def __init__(self, repo: Path) -> None:
        super().__init__()
        self.repo = repo
        self.tt_py = repo / "tt.py"
        self.settings_file = repo / ".tactility" / "gui.json"
        self.settings = self._load_settings()
        self.toolchain_ok, self.toolchain_message = load_espressif_environment(
            str(self.settings.get("idf_export", "")))
        ctk.set_appearance_mode(self.settings.get("appearance", "Dark"))
        ctk.set_default_color_theme("blue")

        self.title("Tactility Dev Console")
        self.geometry(self._initial_geometry())
        self.minsize(1060, 680)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.apps = scan_apps(repo)
        self.selected_app: AppEntry | None = None
        self._app_buttons: list[tuple[AppEntry, ctk.CTkButton]] = []
        self._action_buttons: list[ctk.CTkButton] = []
        self._queue: queue.Queue = queue.Queue()
        self._process: subprocess.Popen | None = None
        self._lock = threading.Lock()
        self._running = False
        self._current_action = ""
        self._started_at = 0.0
        self._save_job: str | None = None
        self._screenshot_target = ""
        self._screenshot_file: Path | None = None
        self._shell_process: subprocess.Popen | None = None
        self.remote_path = "/sdcard"
        self._files_loading = False
        self._files_ready = False
        self._files_root_detected = False
        self._file_rows: list[tuple[str, bool, int | None, ctk.CTkButton, ctk.CTkLabel]] = []
        self._remote_selected = ""
        self._refresh_files_after_job = False
        self._host_save_job: str | None = None
        self._idf_save_job: str | None = None

        self.arch_var = tk.StringVar(value="All manifest platforms")
        self.host_var = tk.StringVar(value=read_host(repo))
        self.idf_export_var = tk.StringVar(value=str(self.settings.get("idf_export", "")))
        self.skip_var = tk.BooleanVar(value=bool(self.settings.get("skip_build", False)))
        self.verbose_var = tk.BooleanVar(value=bool(self.settings.get("verbose", False)))
        self.no_ccache_var = tk.BooleanVar(value=bool(self.settings.get("no_ccache", True)))

        self._build_ui()
        self._restore_selection()
        self._welcome()
        self.after(200, self.focus_set)
        self.after(60, self._poll_queue)
        self.bind("<F5>", lambda _e: self._run("build"))
        self.bind("<F6>", lambda _e: self._run("bir"))
        self.bind("<F9>", lambda _e: self._run("clean"))
        self.bind("<Escape>", lambda _e: self._stop())

    # ---- layout ----
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, weight=0)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        sidebar = ctk.CTkFrame(self, width=300, corner_radius=0, fg_color=("gray90", "gray13"))
        sidebar.grid(row=0, column=0, sticky="nsew")
        sidebar.grid_propagate(False)
        sidebar.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(sidebar, text="Dev Console", font=ctk.CTkFont(size=22, weight="bold"),
                     anchor="w").grid(row=0, column=0, sticky="ew", padx=18, pady=(18, 0))
        ctk.CTkLabel(sidebar, text=self.repo.name, font=ctk.CTkFont(size=12),
                     text_color=("gray40", "gray60"), anchor="w").grid(
            row=1, column=0, sticky="ew", padx=18, pady=(0, 14))

        header = ctk.CTkFrame(sidebar, fg_color="transparent")
        header.grid(row=2, column=0, sticky="ew", padx=18)
        header.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(header, text="Apps", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70")).grid(row=0, column=0, sticky="w")
        ctk.CTkButton(header, text="Refresh", width=70, height=24, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=self._reload_apps).grid(row=0, column=1, sticky="e")

        self.search = ctk.CTkEntry(sidebar, placeholder_text="Filter apps…", height=32)
        self.search.grid(row=3, column=0, sticky="ew", padx=16, pady=(6, 6))
        self.search.bind("<KeyRelease>", lambda _e: self._apply_filter())

        self.app_frame = ctk.CTkScrollableFrame(sidebar, fg_color="transparent", corner_radius=0)
        self.app_frame.grid(row=4, column=0, sticky="nsew", padx=8, pady=(0, 6))
        sidebar.grid_rowconfigure(4, weight=1)

        footer = ctk.CTkFrame(sidebar, fg_color="transparent")
        footer.grid(row=5, column=0, sticky="ew", padx=18, pady=(4, 16))
        ctk.CTkLabel(footer, text="Appearance", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70")).pack(side="left")
        self.appearance_menu = ctk.CTkOptionMenu(footer, values=["System", "Dark", "Light"], width=110,
                                                 command=self._appearance_changed)
        self.appearance_menu.pack(side="right")
        self.appearance_menu.set(self.settings.get("appearance", "Dark"))

        main = ctk.CTkFrame(self, fg_color="transparent")
        main.grid(row=0, column=1, sticky="nsew", padx=18, pady=16)
        main.grid_columnconfigure(0, weight=1)
        main.grid_rowconfigure(2, weight=1)

        top = ctk.CTkFrame(main, fg_color="transparent")
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(0, weight=1)
        self.title_label = ctk.CTkLabel(top, text="—", font=ctk.CTkFont(size=20, weight="bold"),
                                        anchor="w")
        self.title_label.grid(row=0, column=0, sticky="w")
        self.subtitle_label = ctk.CTkLabel(top, text="", font=ctk.CTkFont(size=12),
                                           text_color=("gray40", "gray60"), anchor="w")
        self.subtitle_label.grid(row=1, column=0, columnspan=2, sticky="w")
        self.hint_label = ctk.CTkLabel(top, text="F5 build · F6 run · F9 clean",
                                       font=ctk.CTkFont(size=11),
                                       text_color=("gray50", "gray55"), anchor="w")
        self.hint_label.grid(row=2, column=0, columnspan=2, sticky="w", pady=(2, 0))

        actions = ctk.CTkFrame(top, fg_color="transparent")
        actions.grid(row=0, column=1, sticky="e")
        self._action_button(actions, "Build", "build", accent=True)
        self._action_button(actions, "Run", "bir")
        self._action_button(actions, "Uninstall", "uninstall")
        self._action_button(actions, "Clean", "clean")

        card = ctk.CTkFrame(main)
        card.grid(row=1, column=0, sticky="ew", pady=(14, 0))
        card.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(card, text="Target", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70")).grid(row=0, column=0, sticky="w", padx=(16, 8),
                                                           pady=(14, 4))
        self.arch_menu = ctk.CTkOptionMenu(card, values=list(ARCH_OPTIONS), variable=self.arch_var,
                                           command=lambda _v: self._save_later(), width=190)
        self.arch_menu.grid(row=0, column=1, sticky="w", pady=(14, 4))

        ctk.CTkLabel(card, text="Device host", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70")).grid(row=0, column=2, sticky="e", padx=(18, 8),
                                                           pady=(14, 4))
        self.host_entry = ctk.CTkEntry(card, textvariable=self.host_var, width=190,
                                       placeholder_text="e.g. 192.168.1.50")
        self.host_entry.grid(row=0, column=3, columnspan=2, sticky="ew", padx=(0, 16), pady=(14, 4))
        self.host_entry.bind("<KeyRelease>", self._host_typed)
        self.host_entry.bind("<FocusOut>", self._save_host)
        self.host_entry.bind("<Return>", lambda _e: self._save_host())

        ctk.CTkLabel(card, text="ESP-IDF", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70")).grid(row=1, column=0, sticky="w", padx=(16, 8),
                                                           pady=4)
        self.idf_entry = ctk.CTkEntry(
            card, textvariable=self.idf_export_var,
            placeholder_text="optional: path to export.sh / export.ps1 (saved)")
        self.idf_entry.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        self.idf_entry.bind("<KeyRelease>", self._idf_typed)
        self.idf_entry.bind("<FocusOut>", self._save_idf_setting)
        self.idf_entry.bind("<Return>", self._apply_idf_export)
        idf_actions = ctk.CTkFrame(card, fg_color="transparent")
        idf_actions.grid(row=1, column=3, columnspan=2, sticky="ew", padx=(8, 16), pady=4)
        ctk.CTkButton(idf_actions, text="Browse", width=70,
                      command=self._browse_idf_export).pack(side="left", expand=True, fill="x")
        ctk.CTkButton(idf_actions, text="Apply", width=70,
                      command=self._apply_idf_export).pack(side="left", expand=True, fill="x",
                                                           padx=(8, 0))

        modifiers = ctk.CTkFrame(card, fg_color="transparent")
        modifiers.grid(row=2, column=0, columnspan=5, sticky="ew", padx=16, pady=(2, 14))
        ctk.CTkSwitch(modifiers, text="Skip compiler steps", variable=self.skip_var,
                      command=self._save_later).pack(side="left", padx=(0, 22))
        ctk.CTkSwitch(modifiers, text="Verbose output", variable=self.verbose_var,
                      command=self._save_later).pack(side="left", padx=(0, 22))
        ctk.CTkSwitch(modifiers, text="Disable ccache", variable=self.no_ccache_var,
                      command=self._save_later).pack(side="left")
        self.stop_button = ctk.CTkButton(modifiers, text="Stop", width=70, fg_color="transparent",
                                          border_width=1, text_color=("#a33", "#ff8787"),
                                          border_color=("#c66", "#a33"), command=self._stop)
        self.stop_button.pack(side="right")
        self.stop_button.configure(state="disabled")

        self.tabs = ctk.CTkTabview(main)
        self.tabs.grid(row=2, column=0, sticky="nsew", pady=(14, 0))
        self.tabs.add("Pipeline")
        self.tabs.add("System")
        self.tabs.add("Tools")
        self._build_tools_tab(self.tabs.tab("Tools"))
        self.tabs.add("Shell")
        self._build_shell_tab(self.tabs.tab("Shell"))
        self.tabs.add("Files")
        self._build_files_tab(self.tabs.tab("Files"))
        self.tabs.configure(command=self._tab_changed)

        pipeline_tab = self.tabs.tab("Pipeline")
        command_bar = ctk.CTkFrame(pipeline_tab, fg_color="transparent")
        command_bar.pack(side="bottom", fill="x", padx=8, pady=(0, 8))
        command_bar.grid_columnconfigure(2, weight=1)

        ctk.CTkLabel(command_bar, text="tt", font=ctk.CTkFont(size=12, weight="bold"),
                     text_color=("gray40", "gray60")).grid(row=0, column=0, padx=(2, 6))
        self.command_box = ctk.CTkComboBox(command_bar, values=["app"], width=130,
                                           command=self._command_changed)
        self.command_box.grid(row=0, column=1, sticky="w")
        self.command_args = ctk.CTkEntry(command_bar, placeholder_text="arguments", height=30)
        self.command_args.grid(row=0, column=2, sticky="ew", padx=(8, 0))
        self.command_args.bind("<Return>", lambda _e: self._execute_command_bar())
        self.command_run = ctk.CTkButton(command_bar, text="Run", width=64,
                                         command=self._execute_command_bar)
        self.command_run.grid(row=0, column=3, padx=(8, 0))
        self.command_description = ctk.CTkLabel(command_bar, text="", anchor="w",
                                                font=ctk.CTkFont(size=11),
                                                text_color=("gray50", "gray55"))
        self.command_description.grid(row=1, column=0, columnspan=4, sticky="w", pady=(4, 0))

        self.pipeline_console = Console(pipeline_tab, self._mono_font())
        self.system_console = Console(self.tabs.tab("System"), self._mono_font())
        self.tabs.set("Pipeline")

        bottom = ctk.CTkFrame(main, fg_color="transparent")
        bottom.grid(row=3, column=0, sticky="ew", pady=(12, 0))
        bottom.grid_columnconfigure(0, weight=1)
        self.status_label = ctk.CTkLabel(bottom, text="Idle", anchor="w",
                                         text_color=("gray40", "gray60"))
        self.status_label.grid(row=0, column=0, sticky="w")
        self.progress = ctk.CTkProgressBar(bottom, width=180, mode="indeterminate")
        self.progress.grid(row=0, column=1, sticky="e")
        self.progress.set(0)
        self.progress.grid_remove()

    def _action_button(self, parent, text: str, action: str, accent: bool = False) -> None:
        if accent:
            button = ctk.CTkButton(parent, text=text, width=88, command=lambda: self._run(action))
        else:
            button = ctk.CTkButton(parent, text=text, width=88, fg_color="transparent", border_width=1,
                                   text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                                   command=lambda: self._run(action))
        button.pack(side="left", padx=(0, 8))
        self._action_buttons.append(button)

    @staticmethod
    def _mono_font() -> str:
        if sys.platform == "darwin":
            return "Menlo"
        if os.name == "nt":
            return "Consolas"
        return "DejaVu Sans Mono"

    def _build_tools_tab(self, parent) -> None:
        scroll = ctk.CTkScrollableFrame(parent, fg_color="transparent")
        scroll.pack(fill="both", expand=True, padx=6, pady=6)
        self.tools_scroll = scroll

        files = ctk.CTkFrame(scroll)
        files.pack(fill="x", pady=(0, 10))
        files.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(files, text="Device files", font=ctk.CTkFont(size=13, weight="bold"),
                     anchor="w").grid(row=0, column=0, columnspan=4, sticky="w", padx=16, pady=(12, 2))

        ctk.CTkLabel(files, text="Local", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=58, anchor="w").grid(
            row=1, column=0, sticky="w", padx=(16, 8), pady=4)
        self.push_local = ctk.CTkEntry(files, placeholder_text="file on this computer", height=30)
        self.push_local.grid(row=1, column=1, sticky="ew", pady=4)
        ctk.CTkButton(files, text="Browse", width=70, command=self._browse_push).grid(
            row=1, column=2, padx=8, pady=4)

        ctk.CTkLabel(files, text="Device", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=58, anchor="w").grid(
            row=2, column=0, sticky="w", padx=(16, 8), pady=4)
        self.push_remote = ctk.CTkEntry(files, placeholder_text="absolute path, e.g. /sdcard/file.zip", height=30)
        self.push_remote.grid(row=2, column=1, sticky="ew", pady=4)
        ctk.CTkButton(files, text="Browse", width=70, command=self._browse_remote_push).grid(
            row=2, column=2, padx=8, pady=4)
        ctk.CTkButton(files, text="Push", width=70, command=self._push).grid(
            row=2, column=3, padx=(0, 16), pady=4)

        ctk.CTkFrame(files, height=1, fg_color=("gray80", "gray25")).grid(
            row=3, column=0, columnspan=4, sticky="ew", padx=16, pady=8)

        ctk.CTkLabel(files, text="Device", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=58, anchor="w").grid(
            row=4, column=0, sticky="w", padx=(16, 8), pady=4)
        self.pull_remote = ctk.CTkEntry(files, placeholder_text="absolute path on the device", height=30)
        self.pull_remote.grid(row=4, column=1, sticky="ew", pady=4)
        ctk.CTkButton(files, text="Browse", width=70, command=self._browse_remote_pull).grid(
            row=4, column=2, padx=8, pady=4)
        ctk.CTkButton(files, text="Pull", width=70, command=self._pull).grid(
            row=4, column=3, padx=(0, 16), pady=4)

        ctk.CTkLabel(files, text="Local", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=58, anchor="w").grid(
            row=5, column=0, sticky="w", padx=(16, 8), pady=(4, 12))
        self.pull_local = ctk.CTkEntry(files, placeholder_text="file or folder on this computer", height=30)
        self.pull_local.grid(row=5, column=1, sticky="ew", pady=(4, 12))
        ctk.CTkButton(files, text="Browse", width=70, command=self._browse_pull).grid(
            row=5, column=2, padx=8, pady=(4, 12))

        shot = ctk.CTkFrame(scroll)
        shot.pack(fill="x", pady=(0, 10))
        shot.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(shot, text="Screenshot", font=ctk.CTkFont(size=13, weight="bold"),
                     anchor="w").grid(row=0, column=0, columnspan=4, sticky="w", padx=16, pady=(12, 2))
        ctk.CTkLabel(shot, text="Save to", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=58, anchor="w").grid(
            row=1, column=0, sticky="w", padx=(16, 8), pady=4)
        self.screenshot_path = ctk.CTkEntry(shot, height=30)
        self.screenshot_path.grid(row=1, column=1, sticky="ew", pady=4)
        self.screenshot_path.insert(0, self._default_screenshot_path())
        ctk.CTkButton(shot, text="Browse", width=70, command=self._browse_screenshot).grid(
            row=1, column=2, padx=8, pady=4)
        ctk.CTkButton(shot, text="Capture", width=80, command=self._capture).grid(
            row=1, column=3, padx=(0, 16), pady=4)

        self.screenshot_preview = ctk.CTkLabel(
            shot, text="No screenshot yet", fg_color=CONSOLE_BG,
            text_color=("#9a9aa6", "#9a9aa6"), corner_radius=6, height=120, anchor="center")
        self.screenshot_preview.grid(row=2, column=0, columnspan=4, sticky="ew", padx=16, pady=(6, 4))
        self.screenshot_info = ctk.CTkLabel(shot, text="", anchor="w", font=ctk.CTkFont(size=11),
                                            text_color=("gray50", "gray55"))
        self.screenshot_info.grid(row=3, column=0, columnspan=3, sticky="w", padx=16, pady=(0, 12))
        self.screenshot_open = ctk.CTkButton(shot, text="Open", width=70, state="disabled",
                                             fg_color="transparent", border_width=1,
                                             text_color=("gray10", "gray90"),
                                             border_color=("gray60", "gray40"),
                                             command=self._open_screenshot)
        self.screenshot_open.grid(row=3, column=3, padx=(0, 16), pady=(0, 12))

        device = ctk.CTkFrame(scroll)
        device.pack(fill="x", pady=(0, 10))
        device.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(device, text="Launch & apps", font=ctk.CTkFont(size=13, weight="bold"),
                     anchor="w").grid(row=0, column=0, columnspan=4, sticky="w", padx=16, pady=(12, 2))

        ctk.CTkButton(device, text="Launch simulator", width=140, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"),
                      command=self._launch_simulator).grid(
            row=1, column=0, columnspan=2, sticky="w", padx=16, pady=4)

        ctk.CTkLabel(device, text="Install .app", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=72, anchor="w").grid(
            row=2, column=0, sticky="w", padx=(16, 8), pady=4)
        self.install_file = ctk.CTkEntry(device, placeholder_text="path to .app file", height=30)
        self.install_file.grid(row=2, column=1, sticky="ew", pady=4)
        ctk.CTkButton(device, text="Browse", width=70, command=self._browse_install_file).grid(
            row=2, column=2, padx=8, pady=4)
        ctk.CTkButton(device, text="Install", width=70, command=self._install_app_file).grid(
            row=2, column=3, padx=(0, 16), pady=4)

        ctk.CTkLabel(device, text="Uninstall", font=ctk.CTkFont(size=12),
                     text_color=("gray30", "gray70"), width=72, anchor="w").grid(
            row=3, column=0, sticky="w", padx=(16, 8), pady=4)
        self.uninstall_app = ctk.CTkComboBox(
            device, values=[entry.folder for entry in self.apps], height=30)
        self.uninstall_app.grid(row=3, column=1, sticky="ew", pady=4)
        self.uninstall_app.set("")
        ctk.CTkButton(device, text="Browse", width=70, command=self._browse_uninstall_app).grid(
            row=3, column=2, padx=8, pady=4)
        ctk.CTkButton(device, text="Uninstall", width=70, command=self._uninstall_app_path).grid(
            row=3, column=3, padx=(0, 16), pady=4)

        ctk.CTkLabel(device, text="Install uses tt app install; uninstall uses tt app uninstall with the app's "
                                  "project folder or name.",
                     font=ctk.CTkFont(size=11), text_color=("gray50", "gray55"),
                     anchor="w").grid(row=4, column=0, columnspan=4, sticky="w", padx=16, pady=(4, 12))

        maintenance = ctk.CTkFrame(scroll)
        maintenance.pack(fill="x")
        ctk.CTkLabel(maintenance, text="Maintenance", font=ctk.CTkFont(size=13, weight="bold"),
                     anchor="w").grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(12, 6))

        ctk.CTkButton(maintenance, text="Update SDK", width=140, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"), command=self._update_sdk).grid(
            row=1, column=0, sticky="w", padx=(16, 8), pady=4)
        ctk.CTkButton(maintenance, text="Update tt", width=140, fg_color="transparent",
                      border_width=1, text_color=("gray10", "gray90"),
                      border_color=("gray60", "gray40"), command=self._update_tt).grid(
            row=1, column=1, sticky="w", pady=4)
        ctk.CTkLabel(maintenance, text="Download the latest build of the installed SDK, or update tt and its tools.",
                     font=ctk.CTkFont(size=11), text_color=("gray50", "gray55"),
                     anchor="w").grid(row=2, column=0, columnspan=2, sticky="w", padx=16, pady=(4, 12))

    def _build_shell_tab(self, parent) -> None:
        bar = ctk.CTkFrame(parent, fg_color="transparent")
        bar.pack(fill="x", padx=8, pady=(8, 0))
        ctk.CTkLabel(bar, text="Device shell", font=ctk.CTkFont(size=13, weight="bold")).pack(side="left")
        self.shell_status = ctk.CTkLabel(bar, text="Disconnected", font=ctk.CTkFont(size=11),
                                         text_color=("gray50", "gray55"))
        self.shell_status.pack(side="left", padx=12)

        def bar_button(text: str, command, state: str = "normal", width: int = 90) -> ctk.CTkButton:
            button = ctk.CTkButton(bar, text=text, width=width, fg_color="transparent", border_width=1,
                                   text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                                   command=command, state=state)
            button.pack(side="right", padx=(6, 0))
            return button

        self.shell_connect = ctk.CTkButton(bar, text="Connect", width=90, command=self._start_shell)
        self.shell_connect.pack(side="right", padx=(6, 0))
        self.shell_disconnect = bar_button("Disconnect", self._stop_shell, "disabled")
        self.shell_interrupt = bar_button("Ctrl+C", lambda: self._send_shell_bytes(b"\x03"), "disabled", width=70)
        bar_button("Clear", lambda: self.shell_console.clear(), "normal", width=60)

        input_row = ctk.CTkFrame(parent, fg_color="transparent")
        input_row.pack(side="bottom", fill="x", padx=8, pady=(0, 8))
        input_row.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(input_row, text=">", font=ctk.CTkFont(size=14, weight="bold"), width=18,
                     text_color=("gray40", "gray60")).grid(row=0, column=0)
        self.shell_input = ctk.CTkEntry(input_row, placeholder_text="type a command and press Enter",
                                        height=30, state="disabled")
        self.shell_input.grid(row=0, column=1, sticky="ew")
        self.shell_input.bind("<Return>", lambda _e: self._send_shell())
        self.shell_send = ctk.CTkButton(input_row, text="Send", width=64,
                                        command=self._send_shell, state="disabled")
        self.shell_send.grid(row=0, column=2, padx=(8, 0))

        hint = ctk.CTkLabel(
            parent,
            text=("First connection: compare the fingerprint above with the Development app on the device, "
                  "type y and press Enter, then accept the client on the device."),
            font=ctk.CTkFont(size=11),
            text_color=("gray50", "gray55"),
            anchor="w",
            wraplength=860,
        )
        hint.pack(side="bottom", fill="x", padx=12, pady=(0, 6))

        self.shell_console = Console(parent, self._mono_font())

    def _build_files_tab(self, parent) -> None:
        self._files_dir_font = ctk.CTkFont(size=13, weight="bold")
        self._files_file_font = ctk.CTkFont(size=13)

        bar = ctk.CTkFrame(parent, fg_color="transparent")
        bar.pack(fill="x", padx=8, pady=(8, 0))
        ctk.CTkLabel(bar, text="Device files", font=ctk.CTkFont(size=13, weight="bold")).pack(side="left")
        self.files_status = ctk.CTkLabel(bar, text="Not loaded", font=ctk.CTkFont(size=11),
                                         text_color=("gray50", "gray55"))
        self.files_status.pack(side="left", padx=12)
        ctk.CTkButton(bar, text="Refresh", width=70, fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=self._files_open).pack(side="right")
        ctk.CTkButton(bar, text="Up", width=50, fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=self._files_up).pack(side="right", padx=(0, 6))

        path_row = ctk.CTkFrame(parent, fg_color="transparent")
        path_row.pack(fill="x", padx=8, pady=(6, 0))
        path_row.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(path_row, text="Path", font=ctk.CTkFont(size=12), width=40,
                     text_color=("gray30", "gray70")).grid(row=0, column=0, padx=(0, 6))
        self.files_path = ctk.CTkEntry(path_row, height=30)
        self.files_path.grid(row=0, column=1, sticky="ew")
        self.files_path.insert(0, self.remote_path)
        self.files_path.bind("<Return>", lambda _e: self._files_go(self.files_path.get()))
        ctk.CTkButton(path_row, text="Go", width=50,
                      command=lambda: self._files_go(self.files_path.get())).grid(
            row=0, column=2, padx=(6, 0))

        actions = ctk.CTkFrame(parent, fg_color="transparent")
        actions.pack(side="bottom", fill="x", padx=8, pady=(0, 8))
        self.files_selection = ctk.CTkLabel(actions, text="Select a file to download", anchor="w",
                                            font=ctk.CTkFont(size=11),
                                            text_color=("gray50", "gray55"))
        self.files_selection.pack(side="left", fill="x", expand=True)
        self.files_download = ctk.CTkButton(actions, text="Download", width=90, state="disabled",
                                            command=self._files_download)
        self.files_download.pack(side="right", padx=(8, 0))
        ctk.CTkButton(actions, text="Upload", width=90, fg_color="transparent", border_width=1,
                      text_color=("gray10", "gray90"), border_color=("gray60", "gray40"),
                      command=self._files_upload).pack(side="right")

        self.files_list = ctk.CTkScrollableFrame(parent, fg_color="transparent")
        self.files_list.pack(fill="both", expand=True, padx=8, pady=(8, 8))

    # ---- tools tab actions ----
    def _device_host_arguments(self) -> list[str]:
        host = self.host_var.get().strip()
        return ["--host", host] if host else []

    def _browse_push(self) -> None:
        path = filedialog.askopenfilename(title="File to push")
        if not path:
            return
        self.push_local.delete(0, "end")
        self.push_local.insert(0, path)
        if not self.push_remote.get().strip():
            self.push_remote.insert(0, f"/sdcard/{Path(path).name}")

    def _push(self) -> None:
        local = self.push_local.get().strip()
        remote = self.push_remote.get().strip()
        if not local or not remote:
            self._warn("Push", "Choose a local file and a device path.")
            return
        command = [tool_python(), str(self.tt_py), "push", local, remote,
                   *self._device_host_arguments()]
        self._start_job(command, f"Push {Path(local).name}", str(self.repo))

    def _browse_pull(self) -> None:
        path = filedialog.asksaveasfilename(title="Save pulled file as")
        if not path:
            return
        self.pull_local.delete(0, "end")
        self.pull_local.insert(0, path)

    def _pull(self) -> None:
        remote = self.pull_remote.get().strip()
        if not remote:
            self._warn("Pull", "Enter the path on the device.")
            return
        local = self.pull_local.get().strip() or "."
        command = [tool_python(), str(self.tt_py), "pull", remote, local,
                   *self._device_host_arguments()]
        self._start_job(command, f"Pull {Path(remote).name}", str(self.repo))

    def _browse_remote_push(self) -> None:
        if not self.host_var.get().strip():
            self._warn("Device host", "Enter the device host first.")
            return
        initial = self.push_remote.get().strip() or "/sdcard"
        DevicePathDialog(self, initial, "folder", self._set_push_remote)

    def _set_push_remote(self, path: str) -> None:
        self.push_remote.delete(0, "end")
        self.push_remote.insert(0, path)

    def _browse_remote_pull(self) -> None:
        if not self.host_var.get().strip():
            self._warn("Device host", "Enter the device host first.")
            return
        initial = self.pull_remote.get().strip() or "/sdcard"
        DevicePathDialog(self, initial, "file", self._set_pull_remote)

    def _set_pull_remote(self, path: str) -> None:
        self.pull_remote.delete(0, "end")
        self.pull_remote.insert(0, path)

    def _browse_install_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Install .app file",
            filetypes=[("Tactility app", "*.app"), ("All files", "*.*")],
        )
        if not path:
            return
        self.install_file.delete(0, "end")
        self.install_file.insert(0, path)

    def _install_app_file(self) -> None:
        path = self.install_file.get().strip()
        if not path:
            self._warn("Install app", "Choose an .app file first.")
            return
        if not Path(path).is_file():
            self._warn("Install app", f"File not found: {path}")
            return
        if not self.host_var.get().strip():
            self._warn("Device host", "Enter the device host first.")
            return
        command = [tool_python(), str(self.tt_py), "app", "install", path,
                   *self._device_host_arguments()]
        if self.verbose_var.get():
            command.append("--verbose")
        self._start_job(command, f"Install {Path(path).name}", str(self.repo))

    def _browse_uninstall_app(self) -> None:
        path = filedialog.askdirectory(
            title="Select app project folder",
            initialdir=str(self.repo / "Apps"),
            mustexist=True,
        )
        if not path:
            return
        self.uninstall_app.set(path)

    def _resolve_app_path(self, value: str) -> Path | None:
        value = value.strip()
        if not value:
            return None
        candidate = Path(value).expanduser()
        if candidate.is_dir() and (candidate / "manifest.properties").is_file():
            return candidate.resolve()
        folded = value.casefold()
        for entry in self.apps:
            if folded in (entry.folder.casefold(), entry.app_id.casefold(), entry.name.casefold()):
                return entry.path
        candidate = self.repo / "Apps" / value
        if candidate.is_dir() and (candidate / "manifest.properties").is_file():
            return candidate.resolve()
        return None

    def _uninstall_app_path(self) -> None:
        value = self.uninstall_app.get().strip()
        path = self._resolve_app_path(value)
        if path is None:
            self._warn(
                "Uninstall app",
                "Enter a local app folder or name (for example Calculator), or browse to its project folder.",
            )
            return
        if not self.host_var.get().strip():
            self._warn("Device host", "Enter the device host first.")
            return
        command = [tool_python(), str(self.tt_py), "app", "uninstall", "--path", str(path),
                   *self._device_host_arguments()]
        if self.verbose_var.get():
            command.append("--verbose")
        self._start_job(command, f"Uninstall {path.name}", str(self.repo))

    def _default_screenshot_path(self) -> str:
        relative = Path(".tactility") / "screenshots"
        try:
            (self.repo / relative).mkdir(parents=True, exist_ok=True)
        except OSError:
            pass
        name = datetime.now().strftime("screenshot-%Y-%m-%d-%H%M%S.png")
        return str(relative / name)

    def _browse_screenshot(self) -> None:
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG image", "*.png")],
            initialfile=Path(self.screenshot_path.get().strip() or "screenshot.png").name,
        )
        if not path:
            return
        self.screenshot_path.delete(0, "end")
        self.screenshot_path.insert(0, path)

    def _capture(self) -> None:
        path = self.screenshot_path.get().strip() or self._default_screenshot_path()
        self._screenshot_target = path
        command = [tool_python(), str(self.tt_py), "screenshot", path,
                   *self._device_host_arguments()]
        self._start_job(command, "Screenshot", str(self.repo))

    def _show_screenshot(self, path: Path) -> None:
        self._screenshot_file = path
        self.screenshot_open.configure(state="normal")
        if Image is not None:
            try:
                with Image.open(path) as source:
                    source.thumbnail((900, 160))
                    width, height = source.size
                    image = ctk.CTkImage(light_image=source.copy(), dark_image=source.copy(),
                                         size=(width, height))
            except (OSError, ValueError) as exc:
                self.screenshot_preview.configure(text="Preview unavailable", image=None, height=120)
                self.screenshot_info.configure(text=f"Captured {path} (preview unavailable: {exc})")
                return
            self.screenshot_preview.configure(image=image, text="", height=0)
            self.screenshot_preview.image = image
            self.screenshot_info.configure(text=f"{path}  ·  {width}x{height}")
            return

        # Pillow is optional; Tk's own image support can still show a preview.
        try:
            image = tk.PhotoImage(file=str(path))
        except tk.TclError:
            self.screenshot_preview.configure(text="Preview unavailable", image=None, height=120)
            self.screenshot_info.configure(text=f"Captured {path} (preview unavailable)")
            return
        factor = max(1, image.width() // 420, image.height() // 420)
        if factor > 1:
            image = image.subsample(factor, factor)
        self.screenshot_preview.configure(image=image, text="", height=0)
        self.screenshot_preview.image = image
        self.screenshot_info.configure(text=f"{path}  ·  {image.width()}x{image.height()}")

    def _open_screenshot(self) -> None:
        path = self._screenshot_file
        if path is None or not Path(path).exists():
            return
        try:
            if os.name == "nt":
                os.startfile(str(path))
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(path)])
            else:
                subprocess.Popen(["xdg-open", str(path)])
        except OSError as exc:
            self._error("Open screenshot", str(exc))

    def _launch_simulator(self) -> None:
        self._start_job([tool_python(), str(self.tt_py), "simulator"], "Simulator", str(self.repo))

    def _update_sdk(self) -> None:
        if not self._confirm("Update SDK", "Download the latest build of the installed SDK?"):
            return
        self._start_job([tool_python(), str(self.tt_py), "update-sdk"], "Update SDK", str(self.repo))

    def _update_tt(self) -> None:
        if not self._confirm("Update tt", "Update tt.py and its tools?"):
            return
        self._start_job([tool_python(), str(self.tt_py), "update-self"], "Update tt", str(self.repo))

    # ---- embedded shell ----
    def _start_shell(self) -> None:
        if self._shell_process is not None:
            return
        command = [tool_python(), str(self.tt_py), "shell", *self._device_host_arguments()]
        environment = dict(os.environ)
        environment["PYTHONUNBUFFERED"] = "1"
        environment["PYTHONIOENCODING"] = "utf-8"
        popen_kwargs: dict = {
            "cwd": str(self.repo),
            "env": environment,
            "stdin": subprocess.PIPE,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.STDOUT,
            "bufsize": 0,
        }
        if os.name == "nt":
            popen_kwargs["creationflags"] = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW
        else:
            popen_kwargs["start_new_session"] = True
        try:
            process = subprocess.Popen(command, **popen_kwargs)
        except OSError as exc:
            self._error("Shell", f"Could not start the shell: {exc}")
            return

        with self._lock:
            self._shell_process = process
        self.tabs.set("Shell")
        self.shell_console.clear()
        self.shell_console.stream([(f"$ {' '.join(command)}\n", ("dim",))])
        self.shell_status.configure(text="Connecting…")
        self.shell_connect.configure(state="disabled")
        self.shell_disconnect.configure(state="normal")
        self.shell_interrupt.configure(state="normal")
        self.shell_input.configure(state="normal")
        self.shell_send.configure(state="normal")
        self.shell_input.focus_set()
        threading.Thread(target=self._read_shell, args=(process,), daemon=True).start()

    def _read_shell(self, process: subprocess.Popen) -> None:
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        pending = ""
        waiting_for_accept = False
        connected = False
        while True:
            try:
                chunk = os.read(process.stdout.fileno(), 4096)
            except (OSError, ValueError):
                break
            if not chunk:
                break
            text = decoder.decode(chunk)
            if not text:
                continue
            text = text.replace("\r\n", "\n").replace("\r", "")
            data = pending + text
            incomplete = re.search(r"\x1b(\[[0-9;?]*)?$", data)
            if incomplete:
                pending = data[incomplete.start():]
                data = data[:incomplete.start()]
            else:
                pending = ""
            if data:
                # Emit text before a clear-screen escape, then clear, then the rest.
                while True:
                    positions = [pos for pos in (data.find("\x1b[2J"), data.find("\x1b[3J"))
                                 if pos >= 0]
                    if not positions:
                        break
                    first = min(positions)
                    before = data[:first]
                    if before:
                        self._queue.put(("shell", parse_ansi(before)))
                    self._queue.put(("shell_clear",))
                    data = data[first + 4:]
                plain = CSI_RE.sub("", data)
                accept_marker = "Waiting for the device to accept the connection..."
                handshake_markers = (
                    "The authenticity of", "Device fingerprint:", "Client fingerprint:",
                    "(y/N)", "Continue connecting?", "Accept the new fingerprint",
                )
                if accept_marker in plain:
                    waiting_for_accept = True
                    after = plain.split(accept_marker, 1)[1]
                    if after.strip():
                        connected = True
                        self._queue.put(("shell_status", "Connected"))
                    else:
                        self._queue.put(("shell_status", "Accept the connection on the device"))
                elif waiting_for_accept and not connected:
                    connected = True
                    self._queue.put(("shell_status", "Connected"))
                elif "(y/N)" in plain or "Continue connecting?" in plain or "Accept the new fingerprint" in plain:
                    self._queue.put(("shell_status", "Check the fingerprint above, then type y and press Enter"))
                elif "Device fingerprint:" in plain:
                    self._queue.put(("shell_status", "Compare the fingerprint with the device"))
                elif not connected and not any(marker in plain for marker in handshake_markers):
                    connected = True
                    self._queue.put(("shell_status", "Connected"))
                if data:
                    self._queue.put(("shell", parse_ansi(data)))
        if pending:
            self._queue.put(("shell", parse_ansi(pending)))
        try:
            code = process.wait()
        except OSError:
            code = -1
        with self._lock:
            if self._shell_process is process:
                self._shell_process = None
        self._queue.put(("shell_exit", code))

    def _write_shell(self, process: subprocess.Popen, data: bytes) -> None:
        try:
            process.stdin.write(data)
            process.stdin.flush()
        except (OSError, ValueError) as exc:
            self._system(f"Shell input failed: {exc}", "error")

    def _send_shell(self) -> None:
        process = self._shell_process
        if process is None or process.stdin is None:
            return
        text = self.shell_input.get()
        self.shell_input.delete(0, "end")
        self._write_shell(process, (text + "\n").encode("utf-8"))

    def _send_shell_bytes(self, data: bytes) -> None:
        process = self._shell_process
        if process is not None and process.stdin is not None:
            self._write_shell(process, data)

    def _stop_shell(self) -> None:
        process = self._shell_process
        if process is None:
            return
        self.shell_status.configure(text="Disconnecting…")
        self._terminate(process, "shell")

    def _shell_finished(self, code: int) -> None:
        self.shell_console.stream([(f"\n[shell closed (exit {code})]\n", ("dim",))])
        self.shell_status.configure(text=f"Disconnected (exit {code})")
        self.shell_connect.configure(state="normal")
        self.shell_disconnect.configure(state="disabled")
        self.shell_interrupt.configure(state="disabled")
        self.shell_input.configure(state="disabled")
        self.shell_send.configure(state="disabled")

    def _terminate(self, process: subprocess.Popen, label: str) -> None:
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                               capture_output=True, check=False, creationflags=CREATE_NO_WINDOW)
            else:
                os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        except (OSError, ProcessLookupError) as exc:
            self._system(f"Could not stop the {label}: {exc}", "error")

    # ---- device files ----
    def _tab_changed(self, value: str | None = None) -> None:
        if value is None:
            value = self.tabs.get()
        if value == "Files":
            self._files_open()

    @staticmethod
    def _user_data_root(cwd: str) -> str:
        """The shell starts in <userData>/tactility; expose the user-data root above it."""
        cwd = cwd.rstrip("/") or "/"
        if posixpath.basename(cwd) == "tactility":
            return posixpath.dirname(cwd) or "/"
        return cwd

    def _files_open(self) -> None:
        if self._files_loading:
            return
        if not self._files_root_detected:
            self._files_detect_root()
        else:
            self._files_go(self.remote_path)

    def _files_detect_root(self) -> None:
        self._files_loading = True
        self._files_ready = False
        self._clear_files_list()
        self.files_status.configure(text="Finding device storage…")
        self._files_message("Finding device storage…")
        host_arguments = self._device_host_arguments()
        threading.Thread(target=self._files_pwd_worker, args=(host_arguments,),
                         daemon=True).start()

    def _remote_run(self, command_text: str, host_arguments: list[str]):
        command = [tool_python(), str(self.tt_py), "shell", *host_arguments, command_text]
        environment = dict(os.environ)
        environment["PYTHONUNBUFFERED"] = "1"
        environment["PYTHONIOENCODING"] = "utf-8"
        return subprocess.run(
            command,
            cwd=str(self.repo),
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
            creationflags=CREATE_NO_WINDOW,
        )

    @staticmethod
    def _files_is_trust_error(output: str) -> bool:
        return ("Continue connecting?" in output or "not a known device" in output
                or "run 'tt shell' in a terminal" in output)

    def _files_trust_message(self) -> None:
        self.files_status.configure(text="Device not trusted")
        self._files_message(
            "Trust the device first: open the Shell tab, compare the fingerprint, type y "
            "and press Enter, then accept the client on the device.",
            error=True,
        )

    def _files_pwd_worker(self, host_arguments: list[str]) -> None:
        try:
            result = self._remote_run("pwd", host_arguments)
        except (OSError, subprocess.SubprocessError) as exc:
            self._queue.put(("files_error", str(exc)))
            return
        self._queue.put(("files_pwd", result.returncode, result.stdout))

    def _files_pwd_loaded(self, code: int, output: str) -> None:
        self._files_loading = False
        self._clear_files_list()
        if self._files_is_trust_error(output):
            self._files_trust_message()
            return
        cwd = ""
        for line in output.replace("\r\n", "\n").replace("\r", "\n").splitlines():
            cleaned = CSI_RE.sub("", line).strip()
            if cleaned:
                cwd = cleaned
        if code != 0 or not cwd.startswith("/"):
            self.files_status.configure(text="Could not find the device storage")
            self._files_message(
                "Could not detect the device storage path. Enter /sdcard or /data in the "
                "path box and press Go.",
                error=True,
            )
            return
        root = self._user_data_root(cwd)
        self.remote_path = root
        self._files_root_detected = True
        self.files_path.delete(0, "end")
        self.files_path.insert(0, root)
        self._files_go(root)

    @staticmethod
    def _normalize_remote(path: str) -> str:
        path = path.strip()
        if not path:
            return "/"
        if not path.startswith("/"):
            path = "/" + path
        normalized = posixpath.normpath(path)
        return normalized if normalized.startswith("/") else "/" + normalized

    def _files_go(self, path: str) -> None:
        if self._files_loading:
            return
        normalized = self._normalize_remote(path)
        self.remote_path = normalized
        self._files_root_detected = True
        self.files_path.delete(0, "end")
        self.files_path.insert(0, normalized)
        self._files_loading = True
        self._files_ready = False
        self.files_status.configure(text=f"Loading {normalized}…")
        self._clear_files_list()
        self._files_message("Loading…")
        host_arguments = self._device_host_arguments()
        threading.Thread(target=self._files_worker, args=(normalized, host_arguments),
                         daemon=True).start()

    def _files_worker(self, path: str, host_arguments: list[str]) -> None:
        try:
            result = self._remote_run(f"ls -l {shell_quote(path)}", host_arguments)
        except (OSError, subprocess.SubprocessError) as exc:
            self._queue.put(("files_error", str(exc)))
            return
        self._queue.put(("files", path, result.returncode, result.stdout))

    def _files_loaded(self, path: str, code: int, output: str) -> None:
        self._files_loading = False
        self._clear_files_list()
        if self._files_is_trust_error(output):
            self._files_trust_message()
            return
        if code != 0 or "cannot read" in output:
            self.files_status.configure(text="Could not list the directory")
            first = next((CSI_RE.sub("", line).strip() for line in output.splitlines()
                          if line.strip()), "unknown error")
            self._files_message(first, error=True)
            return

        entries = parse_ls_long(output)
        entries.sort(key=lambda entry: (not entry[1], entry[0].lower()))
        self._files_ready = True
        count = len(entries)
        self.files_status.configure(text=f"{count} item{'s' if count != 1 else ''}")
        if not entries:
            self._files_message("(empty directory)")
            return
        for name, is_dir, size in entries:
            row = ctk.CTkFrame(self.files_list, fg_color="transparent")
            row.pack(fill="x", padx=2, pady=1)
            row.grid_columnconfigure(0, weight=1)
            button = ctk.CTkButton(
                row,
                text=name + ("/" if is_dir else ""),
                anchor="w",
                height=26,
                fg_color="transparent",
                hover_color=("gray85", "gray25"),
                text_color=("#1F6AA5", "#4dabf7") if is_dir else ("gray10", "gray90"),
                font=self._files_dir_font if is_dir else self._files_file_font,
                command=lambda n=name, d=is_dir, s=size: self._files_click(n, d, s),
            )
            button.grid(row=0, column=0, sticky="ew")
            size_label = ctk.CTkLabel(row, text=human_size(size), width=90, anchor="e",
                                      font=ctk.CTkFont(size=11),
                                      text_color=("gray50", "gray55"))
            size_label.grid(row=0, column=1, padx=(8, 8))
            self._file_rows.append((name, is_dir, size, button, size_label))

    def _files_failed(self, message: str) -> None:
        self._files_loading = False
        self._clear_files_list()
        self.files_status.configure(text="Could not list the directory")
        self._files_message(message, error=True)

    def _clear_files_list(self) -> None:
        for child in self.files_list.winfo_children():
            child.destroy()
        self._file_rows = []
        self._remote_selected = ""
        self.files_download.configure(state="disabled")
        self.files_selection.configure(text="Select a file to download")

    def _files_message(self, text: str, error: bool = False) -> None:
        label = ctk.CTkLabel(self.files_list, text=text, anchor="w", justify="left",
                             wraplength=760,
                             text_color=("#a33", "#ff8787") if error else ("gray40", "gray60"))
        label.pack(fill="x", padx=8, pady=8)

    def _files_click(self, name: str, is_dir: bool, size: int | None) -> None:
        if is_dir:
            self._files_go(posixpath.join(self.remote_path, name))
            return
        self._remote_selected = name
        self.files_selection.configure(text=f"{name}  ·  {human_size(size)}")
        self.files_download.configure(state="normal")
        self._style_file_rows()

    def _style_file_rows(self) -> None:
        for name, is_dir, _size, button, _label in self._file_rows:
            if not is_dir and name == self._remote_selected:
                button.configure(fg_color=("#3B8ED0", "#1F6AA5"), text_color="white",
                                 hover_color=("#36719F", "#144870"))
            else:
                button.configure(fg_color="transparent",
                                 hover_color=("gray85", "gray25"),
                                 text_color=("#1F6AA5", "#4dabf7") if is_dir else ("gray10", "gray90"))

    def _files_up(self) -> None:
        parent = posixpath.dirname(self.remote_path) or "/"
        self._files_go(parent)

    def _files_download(self) -> None:
        if not self._remote_selected:
            return
        local = filedialog.asksaveasfilename(title="Save file from device",
                                             initialfile=self._remote_selected)
        if not local:
            return
        remote = posixpath.join(self.remote_path, self._remote_selected)
        command = [tool_python(), str(self.tt_py), "pull", remote, local,
                   *self._device_host_arguments()]
        self._start_job(command, f"Download {self._remote_selected}", str(self.repo),
                        switch_tab=False)

    def _files_upload(self) -> None:
        local = filedialog.askopenfilename(title="Upload file to device")
        if not local:
            return
        name = Path(local).name
        remote = posixpath.join(self.remote_path, name)
        command = [tool_python(), str(self.tt_py), "push", local, remote,
                   *self._device_host_arguments()]
        self._start_job(command, f"Upload {name}", str(self.repo),
                        switch_tab=False, refresh_files=True)

    # ---- apps ----
    def _reload_apps(self) -> None:
        current = self.selected_app.folder if self.selected_app else ""
        self.apps = scan_apps(self.repo)
        if hasattr(self, "uninstall_app"):
            self.uninstall_app.configure(values=[entry.folder for entry in self.apps])
        self._rebuild_app_list(current)
        self._refresh_commands()
        self._system(f"Scanned {len(self.apps)} app{'s' if len(self.apps) != 1 else ''}")

    def _rebuild_app_list(self, selected_folder: str = "") -> None:
        for child in self.app_frame.winfo_children():
            child.destroy()
        self._app_buttons = []
        for app in self.apps:
            button = ctk.CTkButton(self.app_frame, text=app.name, anchor="w", height=30,
                                   corner_radius=8, font=ctk.CTkFont(size=13),
                                   command=lambda a=app: self._select_app(a))
            button.pack(fill="x", padx=2, pady=2)
            self._app_buttons.append((app, button))

        folder = selected_folder or self.settings.get("app", "")
        match = next((app for app in self.apps if app.folder == folder), None)
        if match is None and self.apps:
            match = self.apps[0]
        self.selected_app = match
        self._style_apps()
        self._apply_filter()
        self._update_header()
        self.after(80, self._scroll_to_selected)

    def _apply_filter(self) -> None:
        needle = self.search.get().strip().lower()
        for app, button in self._app_buttons:
            if needle in app.name.lower() or needle in app.folder.lower():
                button.pack(fill="x", padx=2, pady=2)
            else:
                button.pack_forget()
        self._style_apps()

    def _style_apps(self) -> None:
        for app, button in self._app_buttons:
            if self.selected_app and app.folder == self.selected_app.folder:
                button.configure(fg_color=("#3B8ED0", "#1F6AA5"),
                                 hover_color=("#36719F", "#144870"), text_color="white")
            else:
                button.configure(fg_color="transparent", hover_color=("gray85", "gray25"),
                                 text_color=("gray10", "gray90"))

    def _select_app(self, app: AppEntry) -> None:
        self.selected_app = app
        self.settings["app"] = app.folder
        self._style_apps()
        self._update_header()
        self._save_later()
        self._refresh_commands()
        self._system(f"Selected {app.name} ({app.folder})")

    def _update_header(self) -> None:
        app = self.selected_app
        if app is None:
            self.title_label.configure(text="—")
            self.subtitle_label.configure(text="No apps found")
            self.hint_label.configure(text="F5 build · F6 run · F9 clean")
            return
        self.title_label.configure(text=app.name)
        details = [app.app_id] if app.app_id else []
        if app.version:
            details.append(f"v{app.version}")
        if app.sdk:
            details.append(f"SDK {app.sdk}")
        self.subtitle_label.configure(text="  ·  ".join(details))
        targets = f"Targets: {', '.join(app.platforms)}    ·    " if app.platforms else ""
        self.hint_label.configure(
            text=targets + "F5 build · F6 run · F9 clean")

    def _scroll_to_selected(self) -> None:
        if self.selected_app is None:
            return
        button = next((candidate for app, candidate in self._app_buttons
                       if app.folder == self.selected_app.folder), None)
        if button is None:
            return
        canvas = getattr(self.app_frame, "_parent_canvas", None)
        if canvas is None:
            return
        self.app_frame.update_idletasks()
        bounds = canvas.bbox("all")
        if not bounds:
            return
        total = max(1, bounds[3] - bounds[1])
        fraction = button.winfo_y() / total
        canvas.yview_moveto(max(0.0, min(1.0, fraction - 0.25)))

    def _restore_selection(self) -> None:
        architecture = self.settings.get("arch", "auto")
        self.arch_var.set(ARCH_VALUES.get(architecture, "All manifest platforms"))
        self.arch_menu.set(ARCH_VALUES.get(architecture, "All manifest platforms"))
        self._rebuild_app_list()
        self._refresh_commands()

    # ---- system log ----
    def _system(self, text: str, kind: str = "info") -> None:
        tags = {
            "info": ("dim",),
            "ok": ("fg_green",),
            "error": ("fg_red",),
            "warn": ("fg_yellow",),
            "action": ("bold",),
        }.get(kind, ())
        stamp = datetime.now().strftime("%H:%M:%S")
        self.system_console.append([(f"[{stamp}] ", ("dim",)), (text, tags)])

    def _welcome(self) -> None:
        self.system_console.line("Tactility Dev Console", ("bold",))
        self.system_console.line(f"repo: {self.repo}")
        self.system_console.line(f"tool: {self.tt_py}")
        if self.host_var.get():
            self.system_console.line(f"device host: {self.host_var.get()}", ("dim",))
        self._system(self.toolchain_message, "ok" if self.toolchain_ok else "warn")
        self.system_console.line("")
        if not self.apps:
            self._system("No Apps/*/manifest.properties found", "warn")

    # ---- actions ----
    def _run(self, action: str) -> None:
        app = self.selected_app
        if app is None:
            self._system("No app selected", "warn")
            return
        if action in ("install", "run", "uninstall", "bir", "brrr") and not self.host_var.get().strip():
            self._warn("Device host", "Enter the device host first.")
            return
        command = self._build_command(action, app)
        self._start_job(command, ACTION_LABELS.get(action, action), str(app.path))

    def _run_command(self, text: str) -> None:
        tokens = split_command(text)
        if tokens and tokens[0] == "tt":
            tokens = tokens[1:]
        if not tokens:
            self._system("Type a tt command, e.g. help, host, push file, screenshot", "warn")
            return
        command = [tool_python(), str(self.tt_py), *tokens]
        label = f"tt {' '.join(tokens)}"
        if len(label) > 72:
            label = label[:69] + "…"
        self._start_job(command, label, self._command_cwd(tokens[0]))

    def _command_cwd(self, command: str) -> str:
        if command == "app" and self.selected_app is not None:
            return str(self.selected_app.path)
        return str(self.repo)

    def _start_job(self, command: list[str], label: str, cwd: str,
                   switch_tab: bool = True, refresh_files: bool = False) -> None:
        if self._running:
            self._system("Another job is already running", "warn")
            return
        self._current_action = label
        self._started_at = time.time()
        self._running = True
        self._refresh_files_after_job = refresh_files

        if switch_tab:
            self.tabs.set("Pipeline")
        self.pipeline_console.clear()
        self._system(label, "action")
        self._system(f"$ {' '.join(command)}", "info")
        self.status_label.configure(text=label)
        self.progress.grid()
        self.progress.start()
        self._set_busy(True)

        thread = threading.Thread(target=self._worker, args=(command, self._started_at, cwd),
                                  daemon=True)
        thread.start()

    def _execute_command_bar(self) -> None:
        name = self.command_box.get().strip()
        if not name:
            return
        arguments = self.command_args.get().strip()
        self._run_command(name if not arguments else f"{name} {arguments}")

    def _command_tools(self) -> Path | None:
        version = self.selected_app.sdk if self.selected_app is not None else ""
        if not version:
            version = next((app.sdk for app in self.apps if app.sdk), "")
        return sdk_tools_directory(version)

    def _refresh_commands(self) -> None:
        names = discover_commands(self._command_tools())
        current = self.command_box.get().strip() or self.settings.get("command", "app")
        self.command_box.configure(values=names)
        self.command_box.set(current if current in names else ("app" if "app" in names else names[0]))
        self._update_command_description()

    def _update_command_description(self) -> None:
        name = self.command_box.get().strip()
        detail = command_description(self._command_tools(), name)
        if name == "app":
            detail = (detail or "Build, install, run and clean apps") + \
                     "  ·  or use the buttons above, e.g. build --path Apps/Foo"
        self.command_description.configure(text=detail)

    def _command_changed(self, _value=None) -> None:
        self._update_command_description()
        self._save_later()

    def _build_command(self, action: str, app: AppEntry) -> list[str]:
        selection = ARCH_OPTIONS.get(self.arch_var.get(), "auto")
        # No --architecture means "every platform listed in the manifest" to app.py.
        architecture_arguments = [] if selection == "auto" else ["--architecture", selection]

        command = [tool_python(), str(self.tt_py), "app", action, "--path", str(app.path)]
        if self.verbose_var.get():
            command.append("--verbose")
        if self.no_ccache_var.get():
            command.append("--no-ccache")

        host = self.host_var.get().strip()
        host_arguments = ["--host", host] if host else []

        if action == "build":
            command += architecture_arguments
            if self.skip_var.get():
                command.append("--skip-build")
        elif action in ("install", "run", "uninstall"):
            command += host_arguments
        elif action in ("bir", "brrr"):
            command += host_arguments + architecture_arguments
        return command

    def _worker(self, command: list[str], started: float, cwd: str) -> None:
        environment = dict(os.environ)
        environment["PYTHONUNBUFFERED"] = "1"
        environment["PYTHONIOENCODING"] = "utf-8"
        popen_kwargs: dict = {
            "cwd": cwd,
            "env": environment,
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.STDOUT,
            "bufsize": 0,
        }
        if os.name == "nt":
            popen_kwargs["creationflags"] = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW
        else:
            popen_kwargs["start_new_session"] = True

        try:
            process = subprocess.Popen(command, **popen_kwargs)
        except OSError as exc:
            self._queue.put(("start_error", str(exc)))
            return

        with self._lock:
            self._process = process

        # newline="" keeps the carriage returns in progress output, so clean_line() can
        # collapse "⌛ working\r✅ done" into just "✅ done", like a terminal does.
        stdout = io.TextIOWrapper(process.stdout, encoding="utf-8", errors="replace", newline="\n")
        try:
            for raw in stdout:
                text = clean_line(raw)
                self._queue.put(("line", parse_ansi(text)))
        except Exception as exc:  # noqa: BLE001 - never leave the UI stuck in "running"
            self._queue.put(("line", parse_ansi(f"Could not read process output: {exc}")))
        finally:
            try:
                stdout.close()
            except (OSError, ValueError):
                pass
            code = process.wait()
            with self._lock:
                self._process = None
            self._queue.put(("done", code, time.time() - started))

    def _stop(self) -> None:
        with self._lock:
            process = self._process
        if not self._running or process is None:
            return
        self._system("Stopping…", "warn")
        self._terminate(process, "job")

    def _poll_queue(self) -> None:
        try:
            while True:
                message = self._queue.get_nowait()
                kind = message[0]
                if kind == "line":
                    self.pipeline_console.append(message[1])
                elif kind == "done":
                    self._finish(message[1], message[2])
                elif kind == "start_error":
                    self._finish(None, time.time() - self._started_at, message[1])
                elif kind == "shell":
                    self.shell_console.stream(message[1])
                elif kind == "shell_clear":
                    self.shell_console.clear()
                elif kind == "shell_status":
                    self.shell_status.configure(text=message[1])
                elif kind == "shell_exit":
                    self._shell_finished(message[1])
                elif kind == "files":
                    self._files_loaded(message[1], message[2], message[3])
                elif kind == "files_pwd":
                    self._files_pwd_loaded(message[1], message[2])
                elif kind == "files_error":
                    self._files_failed(message[1])
        except queue.Empty:
            pass
        self.after(60, self._poll_queue)

    def _finish(self, code: int | None, elapsed: float, error: str | None = None) -> None:
        self._running = False
        self.progress.stop()
        self.progress.set(0)
        self.progress.grid_remove()
        self._set_busy(False)

        label = self._current_action or "Job"
        if error is not None:
            self.status_label.configure(text=f"{label} failed to start")
            self._system(f"{label} failed to start: {error}", "error")
        elif code == 0:
            self.status_label.configure(text=f"{label} finished in {elapsed:.1f}s")
            self._system(f"{label} finished successfully in {elapsed:.1f}s", "ok")
        else:
            suffix = f" (exit {code})" if code is not None else ""
            self.status_label.configure(text=f"{label} failed{suffix} in {elapsed:.1f}s")
            self._system(f"{label} failed{suffix} in {elapsed:.1f}s", "error")

        if self._current_action == "Screenshot" and code == 0 and self._screenshot_target:
            target = Path(self._screenshot_target)
            if not target.is_absolute():
                target = self.repo / target
            self._screenshot_target = ""
            if target.exists():
                self.tabs.set("Tools")
                self._show_screenshot(target)
                self.screenshot_path.delete(0, "end")
                self.screenshot_path.insert(0, self._default_screenshot_path())

        if self._refresh_files_after_job:
            refresh = self._refresh_files_after_job
            self._refresh_files_after_job = False
            if refresh and code == 0:
                self._files_go(self.remote_path)

    def _set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for button in self._action_buttons:
            button.configure(state=state)
        self.command_box.configure(state=state)
        self.command_args.configure(state=state)
        self.command_run.configure(state=state)
        self.stop_button.configure(state="normal" if busy else "disabled")

    # ---- themed dialogs ----
    def _warn(self, title: str, message: str) -> None:
        if CTkMessagebox is None:
            messagebox.showwarning(title, message)
            return
        CTkMessagebox(master=self, title=title, message=message, icon="warning",
                      option_1="OK", wraplength=380).get()

    def _error(self, title: str, message: str) -> None:
        if CTkMessagebox is None:
            messagebox.showerror(title, message)
            return
        CTkMessagebox(master=self, title=title, message=message, icon="cancel",
                      option_1="OK", wraplength=380).get()

    def _confirm(self, title: str, message: str) -> bool:
        if CTkMessagebox is None:
            return messagebox.askyesno(title, message)
        box = CTkMessagebox(master=self, title=title, message=message, icon="question",
                            option_1="No", option_2="Yes", wraplength=380)
        return box.get() == "Yes"

    # ---- host ----
    def _save_host(self, _event=None) -> None:
        if self._host_save_job is not None:
            try:
                self.after_cancel(self._host_save_job)
            except (tk.TclError, ValueError):
                pass
            self._host_save_job = None
        host = self.host_var.get().strip()
        path = self.repo / ".tactility" / "device_host"
        try:
            current = path.read_text(encoding="utf-8")
        except OSError:
            current = None
        if host:
            if current == host:
                return
        elif current is None:
            return
        write_host(self.repo, host)
        if host:
            self._system(f"Device host set to {host}", "ok")
        else:
            self._system("Device host cleared", "info")

    def _host_typed(self, _event=None) -> None:
        """Saves the host shortly after typing stops."""
        if self._host_save_job is not None:
            try:
                self.after_cancel(self._host_save_job)
            except (tk.TclError, ValueError):
                pass
        self._host_save_job = self.after(900, self._save_host)

    def _idf_typed(self, _event=None) -> None:
        """Saves the ESP-IDF script path shortly after typing stops (Apply still loads it)."""
        if self._idf_save_job is not None:
            try:
                self.after_cancel(self._idf_save_job)
            except (tk.TclError, ValueError):
                pass
        self._idf_save_job = self.after(900, self._save_idf_setting)

    def _save_idf_setting(self, _event=None) -> None:
        if self._idf_save_job is not None:
            try:
                self.after_cancel(self._idf_save_job)
            except (tk.TclError, ValueError):
                pass
            self._idf_save_job = None
        path = self.idf_export_var.get().strip()
        if path == self.settings.get("idf_export", ""):
            return
        self.settings["idf_export"] = path
        self._save_settings()

    def _browse_idf_export(self) -> None:
        path = filedialog.askopenfilename(
            title="ESP-IDF export script",
            filetypes=[
                ("ESP-IDF export", "export.*"),
                ("Shell scripts", "*.sh"),
                ("PowerShell", "*.ps1"),
                ("Batch files", "*.bat"),
                ("Command files", "*.cmd"),
                ("All files", "*.*"),
            ],
        )
        if not path:
            return
        self.idf_export_var.set(path)
        self._apply_idf_export()

    def _apply_idf_export(self, _event=None) -> None:
        if self._idf_save_job is not None:
            try:
                self.after_cancel(self._idf_save_job)
            except (tk.TclError, ValueError):
                pass
            self._idf_save_job = None
        path = self.idf_export_var.get().strip()
        self.settings["idf_export"] = path
        self._save_settings()
        self.config(cursor="watch")
        self.update_idletasks()
        try:
            ok, message = load_espressif_environment(path, force=bool(path))
        finally:
            self.config(cursor="")
        self.toolchain_ok = ok
        self.toolchain_message = message
        self._system(message, "ok" if ok else "error")
        if not ok:
            self._error("ESP-IDF environment", message)

    # ---- settings / close ----
    def _load_settings(self) -> dict:
        defaults = {
            "app": "",
            "arch": "auto",
            "skip_build": False,
            "verbose": False,
            "no_ccache": True,
            "command": "app",
            "idf_export": "",
            "appearance": "Dark",
            "geometry": "",
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
        if match and int(match.group(1)) >= 900 and int(match.group(2)) >= 600:
            return saved
        return "1240x780"

    def _save_later(self) -> None:
        if self._save_job is not None:
            self.after_cancel(self._save_job)
        self._save_job = self.after(400, self._save_settings)

    def _save_settings(self, *_args) -> None:
        self._save_job = None
        geometry = str(self.settings.get("geometry") or "")
        try:
            if self.winfo_ismapped() and self.winfo_width() >= 900 and self.winfo_height() >= 600:
                geometry = self.geometry()
        except tk.TclError:
            pass
        self.settings.update({
            "app": self.selected_app.folder if self.selected_app else "",
            "arch": ARCH_OPTIONS.get(self.arch_var.get(), "auto"),
            "skip_build": bool(self.skip_var.get()),
            "verbose": bool(self.verbose_var.get()),
            "no_ccache": bool(self.no_ccache_var.get()),
            "command": self.command_box.get().strip() or "app",
            "idf_export": self.idf_export_var.get().strip(),
            "appearance": self.appearance_menu.get(),
            "geometry": geometry,
        })
        try:
            self.settings_file.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.settings_file.with_suffix(".tmp")
            temporary.write_text(json.dumps(self.settings, indent=2), encoding="utf-8")
            temporary.replace(self.settings_file)
        except OSError:
            pass

    def _appearance_changed(self, value: str) -> None:
        ctk.set_appearance_mode(value)
        self._save_settings()

    def _on_close(self) -> None:
        if self._running:
            if not self._confirm("Job running", "Stop the running job and exit?"):
                return
            self._stop()
            deadline = time.time() + 3.0
            while self._running and time.time() < deadline:
                self.update()
                time.sleep(0.05)
        self._save_idf_setting()
        self._save_host()
        self._save_settings()
        self._stop_shell()
        self.destroy()


def main() -> int:
    repo = find_repo()
    if repo is None:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(
            "Tactility Dev Console",
            "Run this from a TactilityApps checkout (tt.py and Apps/ were not found).",
        )
        root.destroy()
        return 1
    DevConsole(repo).mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
