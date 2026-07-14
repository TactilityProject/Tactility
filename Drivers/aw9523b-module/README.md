# AW9523B I/O expander

A driver for the `AW9523B` 16-bit I2C-bus I/O expander with LED driver, by Awinic.
Exposes both 8-bit ports (P0/P1) as 16 GPIO pins (0-15, port 0 = pins 0-7, port 1 =
pins 8-15) via the `GPIO_CONTROLLER_TYPE` API. P0 is switched to push-pull mode on
start (every known board wiring for this chip expects push-pull output, not the
open-drain default).

It does not support pull-up/down resistors, high-impedance outputs, or interrupts;
requesting those returns `ERROR_NOT_SUPPORTED`. LED driver functionality is not
implemented.

See https://www.awinic.com/en/productDetail/AW9523B

License: [Apache v2.0](LICENSE-Apache-2.0.md)
