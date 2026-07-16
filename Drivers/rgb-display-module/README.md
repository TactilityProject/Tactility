# RGB Display Driver

A kernel driver for ESP32 RGB (parallel timing-driven) LCD panels, built on ESP-IDF's `esp_lcd_rgb_panel` component. Unlike SPI/i8080 panels, an RGB panel owns its GPIOs directly and has no command interface, so it doesn't sit behind a shared bus controller device - kernel driver equivalent of the deprecated HAL's `RgbDisplay`.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
