import os
import urllib.request

def download_file(url: str, filename: str):
    if not os.path.exists(filename):
        print(f"Downloading {filename} from {url}")
        urllib.request.urlretrieve(url, filename)
    else:
        print(f"{filename} already exists, skipping download.")

def generate(bpp, size, font_file: str, symbols: list, output: str):
    output_file_name = f"{output}_{size}.c"
    output_path = os.path.join("..", "source-fonts", output_file_name)
    print(f"Generating {output_file_name}")
    cmd = "lv_font_conv --no-compress --no-prefilter --bpp {} --size {} --font {} -r {} --format lvgl -o {} --force-fast-kern-format".format(bpp, size, font_file, ",".join(symbols), output_path)
    ret = os.system(cmd)
    if ret != 0:
        raise RuntimeError(f"Font generation failed for {output_file_name} (exit code {ret})")

def read_code_points_map(file_path: str):
    codepoints = {}
    with open(file_path, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                name, cp = parts[0].lower(), parts[1]
                codepoints[name] = cp
    if len(codepoints) == 0:
        raise ValueError(f"Code points map is empty or wasn't found at {file_path}")
    return codepoints


def get_code_points(codepoints, names: list) -> list:
    result = []
    for name in names:
        safe_name = name.lower()
        if not safe_name in codepoints:
            raise ValueError(f"Code point '{safe_name}' not found in map")
        result.append(f"0x{codepoints[safe_name].upper()}")
    return result

def generate_icon_fonts(font_file, font_sizes, symbols, output):
    for size in font_sizes:
        generate(2, size, font_file, symbols, output)

def generate_icon_names(codepoint_map: dict, codepoint_names: list, filename: str):
    print(f"Generating {filename}")
    output_path = os.path.join("..", "include", "tactility", filename)
    with open(output_path, 'w') as f:
        f.write("#pragma once\n\n")
        for name in codepoint_names:
            safe_name = name.lower()
            if safe_name in codepoint_map:
                hex_val = codepoint_map[safe_name]
                # Convert hex to int
                val = int(hex_val, 16)
                # Convert to UTF-8 escaped string
                utf8_bytes = chr(val).encode('utf-8')
                escaped_str = "".join(f"\\x{b:02X}" for b in utf8_bytes)
                
                f.write(f'#define LVGL_SYMBOL_{safe_name.upper()} "{escaped_str}"\n')
            else:
                print(f"Warning: {safe_name} not found in codepoint map")

# --------------- Symbol Fonts ---------------

shared_symbol_code_point_names = [
    "add",
    "apps",
    "area_chart", # System Info
    "app_registration", # App Settings app
    "calendar_month",
    "cable",
    "circle",
    "close",
    "cloud", # Web server
    "check",
    "delete",
    "devices", # Developer app
    "display_settings", # Display Settings app
    "edit_note",
    "electric_bolt", # Power (settings) app
    "folder",
    "deployed_code", # 3D cube
    "download",
    "forum", # Chat app
    "gamepad",
    "help", # Diceware help
    "hub", # App Hub
    "image", # Screenshot app
    "keyboard_arrow_up",
    "lightbulb",
    "language", # Globe
    "lists", # Chat app toolbar
    "mail",
    "menu",
    "mop",
    "more_vert",
    "music_note",
    "note_add",
    "power_settings_new", # Power off for T-Lora Pager
    "refresh", # e.g. App Hub reload button
    "search",
    "settings",
    "toolbar", # Apps without custom icon
    "navigation", # GPS (settings) app
    "keyboard_alt", # Keyboard (settings) app
    "usb", # Power (settings) app
    "wifi", # WiFi (settings) app
]

statusbar_symbol_code_point_names = [
    # Location tracking
    "location_on",
    # Development server
    "cloud",
    # Low on available memory
    "memory",
    # SD Card
    "sd_card",
    "sd_card_alert",
    # Wi-Fi
    "signal_wifi_0_bar",
    "network_wifi_1_bar",
    "network_wifi_2_bar",
    "network_wifi_3_bar",
    "signal_wifi_4_bar",
    "signal_wifi_off",
    "signal_wifi_bad",
    # Battery
    "battery_android_frame_1",
    "battery_android_frame_2",
    "battery_android_frame_3",
    "battery_android_frame_4",
    "battery_android_frame_5",
    "battery_android_frame_6",
    "battery_android_frame_full",
    "battery_android_frame_bolt"
]

launcher_symbol_code_point_names = [
    "apps",
    "folder",
    "settings"
]

shared_symbol_sizes = [
    12,
    16, # T-Deck reference, with montserrat 14 in lv_list as icon with text
    20,
    24,
    32
]

statusbar_symbol_sizes = [
    12,
    16, # T-Deck reference
    20,
    30
]

launcher_symbol_sizes = [
    30,
    36, # T-Deck reference
    42,
    48,
    64,
    72
]

# Resolve file path relative to this script so it can be executed from any CWD
base_dir = os.path.dirname(__file__)
if base_dir:
    os.chdir(base_dir)

codepoints_url = "https://github.com/google/material-design-icons/raw/refs/heads/master/variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.codepoints"
ttf_url = "https://github.com/google/material-design-icons/raw/refs/heads/master/variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf"

codepoints_filename = "MaterialSymbolsRounded.codepoints"
ttf_filename = "MaterialSymbolsRounded.ttf"

download_file(codepoints_url, codepoints_filename)
download_file(ttf_url, ttf_filename)

codepoints_map_path = codepoints_filename
codepoints_map = read_code_points_map(codepoints_map_path)

# Shared symbols
shared_symbol_code_points = get_code_points(codepoints_map, shared_symbol_code_point_names)
generate_icon_fonts(ttf_filename, shared_symbol_sizes, shared_symbol_code_points, "material_symbols_shared")
generate_icon_names(codepoints_map, shared_symbol_code_point_names, "lvgl_symbols_shared.h")

# Statusbar symbols
statusbar_symbol_code_points = get_code_points(codepoints_map, statusbar_symbol_code_point_names)
generate_icon_fonts(ttf_filename, statusbar_symbol_sizes, statusbar_symbol_code_points, "material_symbols_statusbar")
generate_icon_names(codepoints_map, statusbar_symbol_code_point_names, "lvgl_symbols_statusbar.h")

# Launcher symbols
launcher_symbol_code_points = get_code_points(codepoints_map, launcher_symbol_code_point_names)
generate_icon_fonts(ttf_filename, launcher_symbol_sizes, launcher_symbol_code_points, "material_symbols_launcher")
generate_icon_names(codepoints_map, launcher_symbol_code_point_names, "lvgl_symbols_launcher.h")

