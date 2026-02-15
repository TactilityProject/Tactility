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

If you change an icon font size, ensure that a corresponding C file exists in `source-fonts/` (e.g., `material_symbols_shared_24.c`). These files are generated from TTF/OTF fonts using the LVGL font converter.

## License

This module is licensed under the [Apache v2.0](LICENSE-Apache-2.0.md) license.