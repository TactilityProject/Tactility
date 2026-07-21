# XPT2046 Software SPI Touch Driver

A kernel driver for the `XPT2046` resistive touch controller, bit-banged over plain GPIO pins instead of a hardware SPI peripheral. Use this instead of `xpt2046-module` when no SPI host is free for touch (e.g. both `SPI2_HOST` and `SPI3_HOST` are already claimed by the display and an SD card).

No reset or interrupt pin support (matches the controller's typical wiring: polled, no dedicated IRQ line wired on most panels using it).

See https://grobotronics.com/images/datasheets/xpt2046-datasheet.pdf

License: [Apache v2.0](LICENSE-Apache-2.0.md)
