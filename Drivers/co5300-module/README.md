# CO5300 AMOLED display

Tactility display driver for the Chipone CO5300 over QSPI, built on ESP-IDF's
`esp_lcd_co5300` component.

The driver is board-agnostic:

- The vendor bring-up sequence is supplied by the devicetree `init-sequence`
  property using the same `[cmd, data-length, delay-ms, data...]` encoding as
  the other Tactility display drivers.
- Rounded-corner geometry comes from `display.yaml` (`shape` and
  `corner-radius`), so LVGL can inset edge widgets correctly.
- `co5300-backlight` is a child device that controls panel brightness through
  register `0x51`. The panel has no separate backlight GPIO.
- Rotation is implemented through the panel's MADCTL (`swap_xy`/`mirror`), so
  LVGL can rotate the display in hardware.

## Devicetree example

```dts
spi0 {
    compatible = "espressif,esp32-spi";
    host = <SPI2_HOST>;
    cs-gpios = <&gpio0 50 GPIO_FLAG_NONE>;
    pin-sclk = <&gpio0 42 GPIO_FLAG_NONE>;
    pin-mosi = <&gpio0 36 GPIO_FLAG_NONE>;
    pin-miso = <&gpio0 51 GPIO_FLAG_NONE>;
    pin-wp = <&gpio0 35 GPIO_FLAG_NONE>;
    pin-hd = <&gpio0 9 GPIO_FLAG_NONE>;
    max-transfer-size = <65536>;

    display@0 {
        compatible = "chipone,co5300";
        horizontal-resolution = <480>;
        vertical-resolution = <480>;
        shape = <DISPLAY_SHAPE_ROUNDED>;
        corner-radius = <40>;
        pixel-clock-hz = <40000000>;
        pin-reset = <&gpio0 44 GPIO_FLAG_NONE>;

        init-sequence = [
            0x11 0 255
            0xFE 1 0 0x20
            /* ... */
            0x29 0 255
        ];

        display_backlight {
            compatible = "co5300-backlight";
            brightness-default = <255>;
        };
    };
};
```

## Notes

- Colour data is reported as `DISPLAY_COLOR_FORMAT_RGB565_SWAPPED`, because
  the panel expects big-endian RGB565. If a board wires the panel differently,
  the format can be changed in the driver.
- QSPI transfers use PSRAM DMA directly to avoid bounce buffers. If a target
  cannot DMA from PSRAM, set `psram_dma_direct` to 0 in `co5300.cpp`.
- The `init-sequence` delay is a single byte, so each step waits at most 255 ms.
  Use `255` for the sleep-out/display-on waits if the panel tolerates it.
- TE output is consumed when `pin-te` is set: the driver enables it (command 0x35, V-blank
  only) and waits for the rising edge before each transfer. Without `pin-te`, the panel still
  works, but fast animations can tear.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
