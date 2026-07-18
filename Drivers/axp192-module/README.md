# AXP192 power management IC

A driver for the X-Powers `AXP192` PMIC: DCDC1-3/LDO2-3/EXTEN rail enable and voltage
control, battery voltage/current/power readback, charge enable/status, and system power off.
Registers as a `power-supply` child device (voltage, current, is-charging, charge control,
power off) alongside its own public API for full rail control.

Also includes `axp192-backlight`: a `BACKLIGHT_TYPE` driver for a backlight wired to one of
the AXP192's rails, declared as a devicetree child node of the `axp192` device it dims. Its
`rail`, `min-millivolt` and `max-millivolt` properties select the channel and map the 0-255
brightness level onto that rail's voltage range; brightness level 0 disables the rail outright.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
