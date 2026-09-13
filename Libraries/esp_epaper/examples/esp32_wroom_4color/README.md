# ESP32-WROOM-32D + 3.7" 4-Color E-Paper Example

Demo for Good Display ESP32-WROOM-32D Development Kit with GDEY037F51 4-color e-paper panel.

## Hardware

- **MCU**: ESP32-WROOM-32D (No PSRAM)
- **Panel**: GDEY037F51 3.7" 240x416 4-Color (BWRY)
- **Colors**: Black, White, Yellow, Red

## Pinout

| Signal | ESP32 GPIO |
|--------|------------|
| BUSY   | GPIO13     |
| RST    | GPIO12     |
| DC     | GPIO14     |
| CS     | GPIO27     |
| SCK    | GPIO18     |
| MOSI   | GPIO23     |

## Features

- 4 colors: Black, White, Yellow, Red
- 240 x 416 resolution
- 2-bit per pixel format
- No PSRAM required (~25KB buffer)
- No partial refresh support
- Full refresh ~15 seconds

## Build

```bash
cd examples/esp32_wroom_4color
idf.py set-target esp32
idf.py build flash monitor
```

## Links

- [GDEY037F51 Product Page](https://www.good-display.com/product/505.html)
- [Good Display](https://www.good-display.com/)
