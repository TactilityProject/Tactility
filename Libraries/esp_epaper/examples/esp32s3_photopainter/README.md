# ESP32-S3-PhotoPainter Example

Example project for [Waveshare ESP32-S3-PhotoPainter](https://www.waveshare.com/wiki/ESP32-S3-PhotoPainter) - a 7.3" 6-color e-paper photo frame.

## Hardware

- **Board**: Waveshare ESP32-S3-PhotoPainter
- **MCU**: ESP32-S3 with 16MB Flash + 8MB PSRAM (Octal)
- **Panel**: GDEP073E01 (800×480 pixels, 6 colors)
- **Colors**: Black, White, Yellow, Red, Blue, Green
- **Features**: Floyd-Steinberg dithering, SD card slot, RTC

## Pinout

| Signal | GPIO |
|--------|------|
| BUSY   | 13   |
| RST    | 12   |
| DC     | 8    |
| CS     | 9    |
| SCK    | 10   |
| MOSI   | 11   |

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

- E-paper initialization with `EPD_CONFIG_73_6COLOR()` preset
- LVGL 9 integration
- Floyd-Steinberg dithering for smooth gradients
- 6-color palette display
- Grayscale and color gradient demonstration

## Memory Requirements

The 800×480 6-color panel requires significant memory:
- **Framebuffer**: 192 KB (4-bit per pixel)
- **RGB buffer for dithering**: 1.15 MB

PSRAM is required and must be configured in Octal mode for sufficient bandwidth.

## Refresh Time

6-color e-paper panels have a slow refresh time (~15-30 seconds). Partial refresh is not supported on this panel type.

## Dithering

Floyd-Steinberg dithering is enabled by default to provide smooth gradients using only 6 colors. This produces photo-quality results but requires additional processing time.

## License

MIT License
