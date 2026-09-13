# ESP32-WROOM-32D Generic E-Paper Example

Generic example for [Good Display](https://www.good-display.com/) ESP32-WROOM-32D development board with various B/W e-paper panels.

## Hardware

- **Board**: ESP32-WROOM-32D (Good Display dev kit)
- **Panels**: Any SSD1680/SSD1681 based B/W e-paper
- **Default**: GDEY0266T90 (2.66" 152×296)

## Pinout

| Signal | GPIO |
|--------|------|
| BUSY   | 13   |
| RST    | 12   |
| DC     | 14   |
| CS     | 27   |
| SCK    | 18   |
| MOSI   | 23   |

## Supported Panels

Change `EPD_PANEL_TYPE` in `app_main.c` to use different panels:

| Panel Type | Size | Resolution |
|------------|------|------------|
| `EPD_PANEL_SSD16XX_154` | 1.54" | 200×200 |
| `EPD_PANEL_SSD16XX_213` | 2.13" | 122×250 |
| `EPD_PANEL_SSD16XX_266` | 2.66" | 152×296 (default) |
| `EPD_PANEL_SSD16XX_270` | 2.7" | 176×264 |
| `EPD_PANEL_SSD16XX_290` | 2.9" | 128×296 |
| `EPD_PANEL_SSD16XX_370` | 3.7" | 280×480 |
| `EPD_PANEL_SSD16XX_420` | 4.2" | 400×300 |

## Build and Flash

```bash
# Set target
idf.py set-target esp32

# Build
idf.py build

# Flash and monitor
idf.py -p PORT flash monitor
```

## Features Demonstrated

- **Generic SSD16xx Driver**: Same code works for multiple panel sizes
- **Responsive UI**: Layout adapts to panel dimensions automatically
- **Floyd-Steinberg Dithering**: Grayscale simulation on B/W panel
- **Runtime Panel Info**: Displays current panel size and driver info

## Changing Panel at Runtime

To switch panels:

1. Edit `app_main.c` and change `EPD_PANEL_TYPE`
2. Rebuild and flash

The UI will automatically adapt to the new panel size.

## UI Layout

```
┌────────────────────────┐
│    ESP32 E-Paper       │
│ ┌────────────────────┐ │
│ │ Panel: 152x296     │ │
│ │ Generic SSD16xx    │ │
│ └────────────────────┘ │
│ ────────────────────── │
│ Refresh Counter:    0  │
│ Grayscale Demo:        │
│ ██ ▓▓ ▒▒ ░░ ░░ □□    │
│                        │
│   esp_epaper component │
└────────────────────────┘
```

## Custom Configuration

For different pinout, edit the defines in `app_main.c`:

```c
#define EPD_PIN_BUSY    13
#define EPD_PIN_RST     12
#define EPD_PIN_DC      14
#define EPD_PIN_CS      27
#define EPD_PIN_SCK     18
#define EPD_PIN_MOSI    23
```

## License

MIT License
