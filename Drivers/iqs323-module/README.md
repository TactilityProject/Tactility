# IQS323 I2C Driver

A driver for the Azoteq `IQS323` 3-channel capacitive touch controller with on-chip slider and gesture control.

The device is exposed as a `TOUCHBAR_TYPE` device (`tactility/drivers/touchbar.h`).
`drivers/iqs323.h` also adds direct register access, power mode selection, reseed, recalibration, streaming mode and both kinds of reset.

The sensing setup depends on the board's electrodes, so it is given in the devicetree.

The driver loads it after every reset of the chip, calibrates, and then runs the chip in event mode. A calibration that misses its target is repeated up to three times for safety!

See <https://www.azoteq.com/product/iqs323/>

Register addresses and bit layouts were taken from the IQS323 datasheet (v1.13).

License: [Apache v2.0](LICENSE-Apache-2.0.md)
