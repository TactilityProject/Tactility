# CST816T touch controller

A driver for the `CST816T` I2C capacitive touch controller by Hynitron.

Implemented directly against the register. (no `esp_lcd_touch` dependency)

- Manufacturer datasheet: https://github.com/koendv/cst816t/blob/master/extras/DS-CST816T%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8CV2.2.pdf
- Original/reference code: https://github.com/koendv/cst816t (Arduino driver, `src/cst816t.cpp`/`src/cst816t.h`), licensed under [The Unlicense](https://github.com/koendv/cst816t/blob/master/LICENSE)

Single-touch only: the datasheet advertises "genuine two-point" support, but neither of the sources above documents a second touch-point register set, so only one point is read.

`enter_sleep`/`exit_sleep` are unimplemented (`nullptr` in the `PointerApi`, per its "null if not supported" contract).
The reference driver defines `REG_SLEEP_MODE` (0xE5) but never writes it, and the datasheet says waking from sleep needs
a hardware reset pulse anyway, not a register write.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
