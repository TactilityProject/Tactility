# TCA9535 / TCA9539 I/O expander

A driver for the TI `TCA9535` and `TCA9539` 16-bit I2C-bus I/O expanders. Both parts
share the same PCA9555-compatible register map (differing only in package and interrupt
pin): two 8-bit ports for input, output, polarity inversion, and direction, addressed as
pins 0-15 (port 0 = pins 0-7, port 1 = pins 8-15).

It does not support pull-up/down resistors or high-impedance outputs; requesting those
flags returns `ERROR_NOT_SUPPORTED`.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
