# SHTC3 I2C Driver

A driver for the Sensirion `SHTC3` temperature and humidity sensor. Apps read it with
`shtc3_read()` (see `include/drivers/shtc3.h`), which is exported as a module symbol.

See https://sensirion.com/products/catalog/SHTC3

The sensor sleeps between measurements.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
