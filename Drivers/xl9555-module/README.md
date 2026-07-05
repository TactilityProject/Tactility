# XL9555 I/O expander

A driver for the `XL9555` 16-bit I2C-bus I/O expander, used by several LilyGO boards
(e.g. T-Deck Pro Max) for power-rail enables, reset lines, and antenna/audio routing.
Its register map is PCA9555-compatible: two 8-bit ports for input, output, polarity
inversion, and direction, addressed as pins 0-15 (port 0 = pins 0-7, port 1 = pins 8-15).

It does not support pull-up/down resistors or high-impedance outputs; requesting those
flags returns `ERROR_NOT_SUPPORTED`.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
