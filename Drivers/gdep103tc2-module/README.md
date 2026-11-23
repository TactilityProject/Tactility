# gdep103tc2-module

Kernel driver for the E Ink `GDEP103TC2` 10.3" 1872x1404 e-paper panel on an ESP32-S3 with a 16 bit parallel connection.

Supports both black and white (default) and grayscale. The former is faster and doesn't flash, while the latter is slower and needs a waveform supplied by the board in devicetree. The panel's supply rails are owned by the board; the driver only drives an optional enable pin along with the panel's own output and mode lines.

## Sources

Pin maps, bus clocking, gate start sequence and panel parameters are facts taken from [FastEPD](https://github.com/bitbank2/FastEPD) (`ED103TC2` panel and TRMNL X board entries).

## License

[Apache License v2.0](LICENSE-Apache-2.0.md)
