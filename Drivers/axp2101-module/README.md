# AXP2101 power management IC

A driver for the X-Powers `AXP2101` PMIC: DCDC1-5 and ALDO1-4/BLDO1-2/CPUSLDO/DLDO1-2
enable and voltage control, battery/VBUS voltage readback, charge enable/status, and
system power off. Registers as a `power-supply` child device (voltage, is-charging,
charge control, power off) alongside its own public API for full rail control.

Also includes `axp2101-backlight`: a `BACKLIGHT_TYPE` driver for a backlight wired to one of
the AXP2101's LDOs, declared as a devicetree child node of the `axp2101` device it dims. Its
`ldo`, `min-millivolt` and `max-millivolt` properties select the channel and map the 0-255
brightness level onto that LDO's voltage range; brightness level 0 disables the LDO outright.

License: [Apache v2.0](LICENSE-Apache-2.0.md)
