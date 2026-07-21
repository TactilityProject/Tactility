# ST7123 display + touch controller

Two drivers for the Sitronix `ST7123`, sharing this module since they're the same physical chip
family and commonly used together:

- **`st7123`** (`sitronix,st7123`) - the MIPI-DSI display panel itself, driven over ESP-IDF's
  `esp_lcd_st7123` component (DPI/DBI interface, ESP32-P4 and other `SOC_MIPI_DSI_SUPPORTED`
  targets only). Owns the MIPI DSI PHY LDO channel and DSI bus directly, so unlike SPI/RGB panels
  it has no parent bus controller device.
- **`st7123-touch`** (`sitronix,st7123-touch`) - the panel's in-cell capacitive touch controller,
  a separate I2C device, driven over ESP-IDF's `esp_lcd_touch_st7123` component. Sits at a fixed
  I2C address (0x55) - the devicetree `reg` is documentation only, not used to probe the bus.
  Reset is usually wired through the board's IO expander rather than a direct SoC GPIO (every
  known user of this driver so far does it that way), hence `pin-reset` defaults unset; it's
  still exposed for parity with the other touch drivers (e.g. `gt911-module`) in case a board
  wires it directly.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
