# Key Conventions

- Shared cross-platform code uses `#ifdef ESP_PLATFORM` for ESP32-specific paths.
  Code in `Platforms/platform-esp32/` is already ESP-only and does not need guards around ESP-IDF includes.
- The `Drivers/` directory contains hardware drivers (display controllers, touch controllers, PMICs, etc.) — each is its own CMake component.
- `Modules/` contains cross-cutting modules. e.g.`lvgl-module` (LVGL task management).
- `Data/system/` and `Data/data/` are flashed as FAT filesystem images on ESP32 devices that don't have an SD card.
- Translations are in `Translations/` as CSV files, generated via `generate.py`.
