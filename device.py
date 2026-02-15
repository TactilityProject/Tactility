import configparser
import os
import sys
from configparser import ConfigParser

if sys.platform == "win32":
    SHELL_COLOR_RED = ""
    SHELL_COLOR_ORANGE = ""
    SHELL_COLOR_RESET = ""
else:
    SHELL_COLOR_RED = "\033[91m"
    SHELL_COLOR_ORANGE = "\033[93m"
    SHELL_COLOR_RESET = "\033[m"

DEVICES_DIRECTORY = "Devices"

def print_warning(message):
    print(f"{SHELL_COLOR_ORANGE}WARNING: {message}{SHELL_COLOR_RESET}")

def print_error(message):
    print(f"{SHELL_COLOR_RED}ERROR: {message}{SHELL_COLOR_RESET}")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def print_help():
    print("Usage: python device.py [device_id] [arguments]\n\n")
    print(f"\t[device_id]            the device identifier (folder name in {DEVICES_DIRECTORY}/)")
    print("\n")
    print("Optional arguments:\n")
    print("\t--dev                   developer options (limit to 4MB partition table)")

def get_properties_file_path(device_id: str):
    return os.path.join(DEVICES_DIRECTORY, device_id, "device.properties")

def read_file(path: str):
    with open(path, "r") as file:
        result = file.read()
        return result

def read_properties_file(path):
    config = configparser.RawConfigParser()
    # Don't convert keys to lowercase
    config.optionxform = str
    config.read(path)
    return config

def read_device_properties(device_id):
    device_file_path = get_properties_file_path(device_id)
    if not os.path.isfile(device_file_path):
        exit_with_error(f"Device file not found: {device_file_path}")
    return read_properties_file(device_file_path)

def has_group(properties: ConfigParser, group: str):
    return group in properties.sections()

def get_property_or_exit(properties: ConfigParser, group: str, key: str):
    if group not in properties.sections():
        exit_with_error(f"Device properties does not contain group: {group}")
    if key not in properties[group].keys():
        exit_with_error(f"Device properties does not contain key: {key}")
    return properties[group][key]

def get_property_or_default(properties: ConfigParser, group: str, key: str, default):
    if group not in properties.sections():
        return default
    if key not in properties[group].keys():
        return default
    return properties[group][key]

def get_property_or_none(properties: ConfigParser, group: str, key: str):
    return get_property_or_default(properties, group, key, None)

def get_boolean_property_or_false(properties: ConfigParser, group: str, key: str):
    if group not in properties.sections():
        return False
    if key not in properties[group].keys():
        return False
    return properties[group][key] == "true"

def safe_int(value: str, error_message: str):
    try:
        return int(value)
    except ValueError:
        exit_with_error(error_message)

def safe_float(value: str, error_message: str):
    try:
        return float(value)
    except ValueError:
        exit_with_error(error_message)

def write_defaults(output_file):
    default_properties_path = os.path.join("Buildscripts", "sdkconfig", "default.properties")
    default_properties = read_file(default_properties_path)
    output_file.write(default_properties)

def write_partition_table(output_file, device_properties: ConfigParser, is_dev: bool):
    if is_dev:
        flash_size_number = 4
    else:
        flash_size = get_property_or_exit(device_properties, "hardware", "flashSize")
        if not flash_size.endswith("MB"):
            exit_with_error("Flash size should be written as xMB or xxMB (e.g. 4MB, 16MB)")
        flash_size_number = flash_size[:-2]
    output_file.write("# Partition Table\n")
    output_file.write("CONFIG_PARTITION_TABLE_CUSTOM=y\n")
    output_file.write(f"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions-{flash_size_number}mb.csv\"\n")
    output_file.write(f"CONFIG_PARTITION_TABLE_FILENAME=\"partitions-{flash_size_number}mb.csv\"\n")

def write_tactility_variables(output_file, device_properties: ConfigParser, device_id: str):
    # Board and vendor
    board_vendor = get_property_or_exit(device_properties, "general", "vendor").replace("\"", "\\\"")
    board_name = get_property_or_exit(device_properties, "general", "name").replace("\"", "\\\"")
    if board_name == board_vendor or board_vendor == "":
        output_file.write(f"CONFIG_TT_DEVICE_NAME=\"{board_name}\"\n")
    else:
        output_file.write(f"CONFIG_TT_DEVICE_NAME=\"{board_vendor} {board_name}\"\n")
    output_file.write(f"CONFIG_TT_DEVICE_ID=\"{device_id}\"\n")
    if device_id == "lilygo-tdeck":
        output_file.write("CONFIG_TT_TDECK_WORKAROUND=y\n")
    # Launcher app id
    launcher_app_id = get_property_or_exit(device_properties, "apps", "launcherAppId").replace("\"", "\\\"")
    output_file.write(f"CONFIG_TT_LAUNCHER_APP_ID=\"{launcher_app_id}\"\n")
    # Auto start app id
    auto_start_app_id = get_property_or_none(device_properties, "apps", "autoStartAppId")
    if auto_start_app_id is not None:
        safe_auto_start_app_id = auto_start_app_id.replace("\"", "\\\"")
        output_file.write(f"CONFIG_TT_AUTO_START_APP_ID=\"{safe_auto_start_app_id}\"\n")

def write_core_variables(output_file, device_properties: ConfigParser):
    idf_target = get_property_or_exit(device_properties, "hardware", "target").lower()
    output_file.write("# Target\n")
    output_file.write(f"CONFIG_IDF_TARGET=\"{idf_target}\"\n")
    output_file.write("# CPU\n")
    output_file.write("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y\n")
    output_file.write("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240\n")
    output_file.write(f"CONFIG_{idf_target.upper()}_DEFAULT_CPU_FREQ_240=y\n")
    output_file.write(f"CONFIG_{idf_target.upper()}_DEFAULT_CPU_FREQ_MHZ=240\n")
    if idf_target != "esp32": # Not available on original ESP32
        output_file.write("# Enable usage of MALLOC_CAP_EXEC on IRAM:\n")
        output_file.write("CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n\n")
        output_file.write("CONFIG_ESP_SYSTEM_MEMPROT_FEATURE_LOCK=n\n")

def write_flash_variables(output_file, device_properties: ConfigParser):
    flash_size = get_property_or_exit(device_properties, "hardware", "flashSize")
    if not flash_size.endswith("MB"):
        exit_with_error("Flash size should be written as xMB or xxMB (e.g. 4MB, 16MB)")
    output_file.write("# Flash\n")
    flash_size_number = flash_size[:-2]
    output_file.write(f"CONFIG_ESPTOOLPY_FLASHSIZE_{flash_size_number}MB=y\n")
    flash_mode = get_property_or_default(device_properties, "hardware", "flashMode", 'QIO')
    output_file.write(f"CONFIG_FLASHMODE_{flash_mode}=y\n")
    esptool_flash_freq = get_property_or_none(device_properties, "hardware", "esptoolFlashFreq")
    if esptool_flash_freq is not None:
        output_file.write(f"CONFIG_ESPTOOLPY_FLASHFREQ_{esptool_flash_freq}=y\n")

def write_spiram_variables(output_file, device_properties: ConfigParser):
    idf_target = get_property_or_exit(device_properties, "hardware", "target").lower()
    has_spiram = get_property_or_exit(device_properties, "hardware", "spiRam")
    if has_spiram != "true":
        return
    output_file.write("# SPIRAM\n")
    # Boot speed optimization
    output_file.write("CONFIG_SPIRAM_MEMTEST=n\n")
    # Enable
    output_file.write("CONFIG_SPIRAM=y\n")
    output_file.write(f"CONFIG_{idf_target.upper()}_SPIRAM_SUPPORT=y\n")
    mode = get_property_or_exit(device_properties, "hardware", "spiRamMode")
    if mode == "OPI":
        mode = "OCT"
    # Mode
    if mode != "AUTO":
        output_file.write(f"CONFIG_SPIRAM_MODE_{mode}=y\n")
    else:
        output_file.write("CONFIG_SPIRAM_TYPE_AUTO=y\n")
    speed = get_property_or_exit(device_properties, "hardware", "spiRamSpeed")
    # Speed
    output_file.write(f"CONFIG_SPIRAM_SPEED_{speed}=y\n")
    output_file.write(f"CONFIG_SPIRAM_SPEED={speed}\n")
    # Reduce IRAM usage
    output_file.write("CONFIG_SPIRAM_USE_MALLOC=y\n")
    output_file.write("CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y\n")
    # Performance improvements
    if idf_target == "esp32s3":
        output_file.write("CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\n")
        output_file.write("CONFIG_SPIRAM_RODATA=y\n")
        output_file.write("CONFIG_SPIRAM_XIP_FROM_PSRAM=y\n")

def write_performance_improvements(output_file, device_properties: ConfigParser):
    idf_target = get_property_or_exit(device_properties, "hardware", "target").lower()
    if idf_target == "esp32s3":
        output_file.write("# Performance improvement: Fixes glitches in the RGB display driver when rendering new screens/apps\n")
        output_file.write("CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y\n")

def write_lvgl_variable_placeholders(output_file):
    output_file.write("# LVGL Placeholder\n")
    output_file.write("CONFIG_LV_DPI_DEF=100\n")
    output_file.write("CONFIG_LV_FONT_MONTSERRAT_8=y\n")
    output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_8=y\n")
    output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=8\n")
    output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=8\n")
    output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=8\n")
    output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=12\n")
    output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=30\n")
    output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=12\n")

def write_lvgl_variables(output_file, device_properties: ConfigParser):
    output_file.write("# LVGL\n")
    if not has_group(device_properties, "lvgl") or not has_group(device_properties, "display"):
        write_lvgl_variable_placeholders(output_file)
        return
    # LVGL DPI overrides the real DPI settings
    dpi_text = get_property_or_none(device_properties, "lvgl", "dpi")
    if dpi_text is None:
        dpi_text = get_property_or_exit(device_properties, "display", "dpi")
    dpi = safe_int(dpi_text, f"DPI must be an integer, but was: '{dpi_text}'")
    output_file.write(f"CONFIG_LV_DPI_DEF={dpi}\n")
    color_depth = get_property_or_exit(device_properties, "lvgl", "colorDepth")
    output_file.write(f"CONFIG_LV_COLOR_DEPTH={color_depth}\n")
    output_file.write(f"CONFIG_LV_COLOR_DEPTH_{color_depth}=y\n")
    output_file.write("CONFIG_LV_DISP_DEF_REFR_PERIOD=10\n")
    theme = get_property_or_default(device_properties, "lvgl", "theme", "DefaultDark")
    if theme == "DefaultDark":
        output_file.write("CONFIG_LV_THEME_DEFAULT_DARK=y\n")
    elif theme == "DefaultLight":
        output_file.write("CONFIG_LV_THEME_DEFAULT_LIGHT=y\n")
    elif theme == "Mono":
        output_file.write("CONFIG_LV_THEME_DEFAULT_DARK=y\n")
        output_file.write("CONFIG_LV_THEME_MONO=y\n")
    else:
        exit_with_error(f"Unknown theme: {theme}")
    font_height_text = get_property_or_default(device_properties, "lvgl", "fontSize", "14")
    font_height = safe_int(font_height_text, f"Font height must be an integer, but was: '{font_height_text}'")
    if font_height <= 12:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_8=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_12=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_16=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_12=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=8\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=12\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=16\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=12\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=30\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=12\n")
    elif font_height <= 14:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_10=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_14=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_18=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=10\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=14\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=18\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=16\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=36\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=16\n")
    elif font_height <= 16:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_12=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_16=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_22=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_16=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=12\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=16\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=22\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=16\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=42\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=16\n")
    elif font_height <= 18:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_14=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_18=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_24=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_18=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=14\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=18\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=24\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=20\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=48\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=20\n")
    elif font_height <= 24:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_18=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_24=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_30=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_24=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=18\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=24\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=30\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=20\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=64\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=24\n")
    else:
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_20=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_28=y\n")
        output_file.write("CONFIG_LV_FONT_MONTSERRAT_36=y\n")
        output_file.write("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_28=y\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_SMALL=20\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_DEFAULT=28\n")
        output_file.write("CONFIG_TT_LVGL_FONT_SIZE_LARGE=36\n")
        output_file.write("CONFIG_TT_LVGL_STATUSBAR_ICON_SIZE=30\n")
        output_file.write("CONFIG_TT_LVGL_LAUNCHER_ICON_SIZE=72\n")
        output_file.write("CONFIG_TT_LVGL_SHARED_ICON_SIZE=32\n")


def write_usb_variables(output_file, device_properties: ConfigParser):
    has_tiny_usb = get_boolean_property_or_false(device_properties, "hardware", "tinyUsb")
    if has_tiny_usb:
        output_file.write("# TinyUSB\n")
        output_file.write("CONFIG_TINYUSB_MSC_ENABLED=y\n")
        output_file.write("CONFIG_TINYUSB_MSC_MOUNT_PATH=\"/sdcard\"\n")

def write_custom_sdkconfig(output_file, device_properties: ConfigParser):
    if "sdkconfig" in device_properties.sections():
        output_file.write("# Custom\n")
        section = device_properties["sdkconfig"]
        for key in section.keys():
            value = section[key].replace("\"", "\\\"")
            output_file.write(f"{key}={value}\n")

def write_properties(output_file, device_properties: ConfigParser, device_id: str, is_dev: bool):
    write_defaults(output_file)
    output_file.write("\n\n")
    write_tactility_variables(output_file, device_properties, device_id)
    write_core_variables(output_file, device_properties)
    write_flash_variables(output_file, device_properties)
    write_partition_table(output_file, device_properties, is_dev)
    write_spiram_variables(output_file, device_properties)
    write_performance_improvements(output_file, device_properties)
    write_usb_variables(output_file, device_properties)
    write_custom_sdkconfig(output_file, device_properties)
    write_lvgl_variables(output_file, device_properties)

def main(device_id: str, is_dev: bool):
    device_properties_path = get_properties_file_path(device_id)
    if not os.path.isfile(device_properties_path):
        exit_with_error(f"{device_id} is not a valid device identifier (could not found {device_properties_path})")
    output_file_path = "sdkconfig"
    if os.path.isfile(output_file_path):
        os.remove(output_file_path)
    device_properties = read_device_properties(device_id)
    with open(output_file_path, "w") as output_file:
        write_properties(output_file, device_properties, device_id, is_dev)
    if is_dev:
        dev_mode_postfix = " in dev mode"
    else:
        dev_mode_postfix = ""
    print(f"Created sdkconfig for {device_id}{dev_mode_postfix}")

if __name__ == "__main__":
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    if len(sys.argv) < 2:
        print_help()
        sys.exit()
    is_dev = "--dev" in sys.argv
    main(sys.argv[1], is_dev)
