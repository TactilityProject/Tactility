# BQ27427 I2C Driver

A driver for the Texas Instruments `BQ27427` single-cell Impedance Track fuel gauge.
I2C address `0x55`

It creates a `power-supply` child device (current, voltage, charge level, charging state).

`drivers/bq27427.h` also exposes every standard command of the gauge in one snapshot, and the read-only and parameterless `Control()` subcommands.

Capacities in the snapshot are always in mAh: the driver multiplies them by the gauge's Design Energy Scale. That scale is 10 for packs over 6 Ah, which makes the gauge count in units of 10 mAh and 10 mWh.

The gauge starts with a default 1340 mAh profile, so a board should configure it for its battery. A profile is a list of 32-byte data memory blocks (`Bq27427Profile`):

- `bq27427_profile_needed()` tells whether the gauge has to be programmed (it lost its RAM configuration, has another Design Energy Scale, or reports current with inverted polarity, etc, etc).
- `bq27427_apply_profile()` unseals the gauge (the default key and the keys the TRMNL firmware uses), writes and verifies every block, makes charging current positive, soft resets and seals it again. Note that it blocks for several seconds.
- `bq27427_wait_ready()` waits for the gauge to finish its initialization.

The gauge's watchdog can reset it when it is polled too often, so snapshots are cached for one second for safety.

See <https://www.ti.com/lit/pdf/sluucd5> (BQ27427 Technical Reference Manual) and <https://www.ti.com/product/BQ27427>

License: [Apache v2.0](LICENSE-Apache-2.0.md)
