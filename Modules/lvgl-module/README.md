# lvgl-module

This module manages the lifecycle of the [LVGL](https://lvgl.io/) library within the Tactility ecosystem. 

## What the library does

The `lvgl-module` provides:
- **Lifecycle Management**: Handles initialization and termination of the LVGL library.
- **Task Management**: Manages the LVGL main loop task.
- **Thread-Safety**: Provides mutex-based locking mechanisms (`lvgl_lock`, `lvgl_unlock`) to ensure safe access to LVGL APIs from multiple tasks.
- **Font Access**: Provides a unified interface to access pre-configured text and icon fonts.

## Different types of fonts

The module supports two main categories of fonts:

### Text Fonts

Standard text rendering uses the **Montserrat** font. Three sizes are pre-configured:
- `FONT_SIZE_SMALL`
- `FONT_SIZE_DEFAULT`
- `FONT_SIZE_LARGE`

### Icon Fonts

Icons are provided by the **Material Symbols** font, divided into three usage-specific sets:
- **Statusbar Icons**: Optimized for the system status bar.
- **Launcher Icons**: Sized for application launchers.
- **Shared Icons**: General purpose icons used across the system.

## How to update the fonts

Font sizes and symbols are configurable:

- **On ESP32 (IDF)**: Sizes can be updated via `menuconfig` or by editing `sdkconfig`. Look for `CONFIG_TT_LVGL_FONT_SIZE_*` and `CONFIG_TT_LVGL_*_ICON_SIZE` parameters.
- **On Simulator/POSIX**: Default sizes are defined in `Modules/lvgl-module/CMakeLists.txt`.

Icon fonts are rasterized on device at startup from `Data/system/fonts/MaterialSymbolsRounded.ttf` (a subset with only the used icons) and cached as `.bin` files in the data path, so any icon size works.
The icons per font are listed by name and resolved through `Data/system/fonts/MaterialSymbolsRounded.codepoints`.
To add an icon, add its name to `generate-icons.py` and run it (requires `pip install fonttools`). This regenerates the TTF subset, the codepoints file and the name lists.

## Custom memory allocator

LVGL's malloc/realloc/free can be routed through a custom backend instead of its built-in pool or
plain `malloc`. Three things are required:

**1. Select the backend.** On ESP32, via Kconfig (`sdkconfig`):
```sdkconfig
CONFIG_LV_USE_CUSTOM_MALLOC=y
```
On Simulator/POSIX, ESP-IDF's Kconfig doesn't apply - select it in `lv_conf.h` instead:
```c
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
```

**2. Force the module into the link.** LVGL's own init code calls back into the functions above
a reverse reference a normal single-pass static-archive link can't resolve on its own:
```cmake
tactility_add_module(lvgl-module
    ...
    WHOLE_ARCHIVE
)
```
Without `WHOLE_ARCHIVE`, or without step 1 selecting the custom backend, ESP-IDF's LVGL component
compiles its own allocator using these same symbol names - whichever one the linker happens to
pull in first silently wins, with no error and no guarantee it's the intended one.

## License

This module is licensed under the [Apache v2.0](LICENSE-Apache-2.0.md) license.