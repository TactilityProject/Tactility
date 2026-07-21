# ST7789 i8080 Display Driver

A kernel driver for the `ST7789` display panel over an i8080 (parallel) bus, built on ESP-IDF's `esp_lcd` component. Child of an i8080 bus controller device (e.g. `espressif,esp32-i8080` in `Platforms/platform-esp32`) - use `st7789-module` instead for SPI-attached ST7789 panels.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
