# HX8357 Display Driver

A kernel driver for the `HX8357-D` display panel (24bpp/RGB888). No ESP-IDF `esp_lcd` component exists for this controller, so this driver speaks its raw SPI command protocol directly rather than wrapping `esp_lcd_panel_io`/`esp_lcd_panel`.

See https://cdn-shop.adafruit.com/datasheets/HX8357-D_DS_April2012.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
