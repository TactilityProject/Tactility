# CH422G I/O expander

A driver for the `CH422G` I2C-bus I/O expander by WCH, commonly used on Waveshare ESP32
display boards to drive display/touch reset and enable lines.

Exposes EXIO0-7 as a `GPIO_CONTROLLER_TYPE` device. The chip has no register to configure
per-pin direction and no readback for its output register, so only output pins are supported
and the driver keeps a local shadow of the last written value.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
