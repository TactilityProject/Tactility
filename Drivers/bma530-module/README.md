# BMA530 I2C Driver

A driver for the `BMA530` 3-axis accelerometer.

Besides the generic `ImuApi` accelerometer and temperature readout, `drivers/bma530.h` exposes the sensor's configuration, offsets, sensor time, interrupt routing and the on-chip feature engine (orientation, step counter, activity)!

See <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bma530-ds000.pdf>

Register addresses and bit layouts were taken from Bosch's BSD-3-Clause [BMA530 SensorAPI](https://github.com/boschsensortec/BMA530_SensorAPI).

License: [Apache v2.0](LICENSE-Apache-2.0.md)
