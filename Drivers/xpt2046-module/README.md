# XPT2046 SPI Touch Driver

A kernel driver for the `XPT2046` resistive touch controller, built on ESP-IDF's `esp_lcd_touch_xpt2046` component over the standard SPI master driver.

No reset or interrupt pin support (matches the controller's typical wiring: polled over SPI, no dedicated IRQ line wired on most panels using it).

See https://grobotronics.com/images/datasheets/xpt2046-datasheet.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
