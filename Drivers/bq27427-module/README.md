# BQ27427 I2C Driver

A driver for the Texas Instruments `BQ27427` single-cell Impedance Track fuel gauge.
I2C address `0x55`

It creates a `power-supply` child device (current, voltage, charge level, charging state).

`drivers/bq27427.h` also exposes every standard command of the gauge in one snapshot, the read-only and parameterless `Control()` subcommands, and a routine to program the cell's design capacity, design energy and terminate voltage into data memory.

The data memory properties in the devicetree are optional. When set, the gauge is only reprogrammed if it holds different values.

The gauge's watchdog can reset it when it is polled too often, so snapshots are cached for one second for safety

See <https://www.ti.com/lit/pdf/sluucd5> (BQ27427 Technical Reference Manual) and <https://www.ti.com/product/BQ27427>

License: [Apache v2.0](LICENSE-Apache-2.0.md)
