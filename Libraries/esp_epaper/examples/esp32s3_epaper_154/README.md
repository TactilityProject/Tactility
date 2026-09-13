# ESP32-S3-ePaper-1.54 Example

Example project for [Waveshare ESP32-S3-ePaper-1.54](https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54) development board.

## Hardware

- **Board**: Waveshare ESP32-S3-ePaper-1.54
- **MCU**: ESP32-S3 with 4MB Flash + 2MB PSRAM
- **Panel**: GDEY0154D67 (200×200 pixels, Black/White)
- **Features**: Partial refresh support, RTC, Temperature sensor

## Pinout

| Signal | GPIO |
|--------|------|
| BUSY   | 8    |
| RST    | 9    |
| DC     | 10   |
| CS     | 11   |
| SCK    | 12   |
| MOSI   | 13   |
| PWR_EN | 6    |

## Build and Flash

```bash
# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py -p PORT flash monitor
```

## Features Demonstrated

- E-paper initialization with `EPD_CONFIG_ESP32S3_154()` preset
- LVGL 9 integration
- **Floyd-Steinberg dithering** for grayscale simulation on B/W panel
- Partial refresh for fast updates
- Periodic full refresh to clear ghosting
- Board power control (GPIO6)

## Demo UI

The example displays:

1. **8-level grayscale bar** - Demonstrates dithering quality with discrete gray steps
2. **Continuous gradient** - Shows smooth black-to-white transition using dithering
3. **Gray boxes** - 4 boxes with different gray levels (0%, 25%, 50%, 75%)
4. **Counter** - Updates every 10 seconds using partial refresh

## Usage

The example displays a counter that updates every 10 seconds using partial refresh. Every 5 updates, a full refresh is performed to clear any ghosting artifacts.

The grayscale dithering uses Floyd-Steinberg error diffusion to simulate gray levels on the B/W panel. This creates the illusion of more than 2 colors by varying the density of black pixels.

## License

MIT License
