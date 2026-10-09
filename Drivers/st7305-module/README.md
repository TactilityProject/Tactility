# ST7305 display controller

A driver for the Sitronix `ST7305` monochrome reflective LCD controller (`sitronix,st7305`), connected over SPI.

- Original/reference code: https://github.com/Kevincoooool/esp_lcd_st7305, licensed under [Apache v2.0](https://github.com/Kevincoooool/esp_lcd_st7305/blob/master/license.txt)

The init sequence and RAM layout are based on the reference's 2.9" (384x168) panel configuration.
The panel is driven in full-frame mode: each frame is converted from LVGL's 1bpp format into the controller's
interleaved 2-line RAM layout before it is sent.
Mirroring is applied in software during that conversion.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
