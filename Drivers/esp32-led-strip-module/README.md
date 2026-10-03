# esp32-led-strip-module

WS2812-compatible RGB LED strip driver for Tactility. It uses ESP-IDF's RMT peripheral to transmit pixel data and provides per-pixel and range color control, brightness adjustment, and gamma-corrected output.

## Devicetree Configuration

Declare the strip with the compatible string `"espressif,esp32-led-strip"`. `pin-data` references the GPIO device and pin used for the data signal; `led-count` is the number of LEDs on the strip.

```dts
ws2812 {
    compatible = "espressif,esp32-led-strip";
    pin-data = <&gpio0 14 GPIO_FLAG_NONE>;
    led-count = <8>;
};
```

The driver reads these properties into `LedStripConfig`.

## API

Include `<tactility/drivers/led_strip.h>`. The kernel API operates on a `struct Device *` and returns `error_t`; successful calls return `ERROR_NONE`.

| Function | Description |
|---|---|
| `led_strip_set_single_led_color` | Set one LED using global brightness |
| `led_strip_set_single_led_color_and_brightness` | Set one LED with an explicit brightness |
| `led_strip_set_led_range_color` | Set a range from an array of `LedRgb` values using global brightness |
| `led_strip_set_led_range_color_and_brightness` | Set a range from an array with explicit brightness |
| `led_strip_fill_led_range_color` | Fill a range using global brightness |
| `led_strip_fill_led_range_color_and_brightness` | Fill a range with explicit brightness |
| `led_strip_set_brightness` | Set global brightness |
| `led_strip_show` | Transmit buffered pixel values to the strip |
| `led_strip_clear` | Turn off all LEDs |
| `led_strip_get_length` | Get the configured LED count |

LED indices and range offsets are zero-based. Color channels in `struct LedRgb` range from 0 to 255. Global and per-operation brightness also range from 0 (off) to 255 (full); the default global brightness is 255. Call `led_strip_show()` after setting colors to transmit the updated buffer. `led_strip_clear()` clears and refreshes the strip.

## Usage Example

```c
#include <tactility/drivers/led_strip.h>

void led_demo(struct Device *strip) {
    uint16_t length = 0;
    if (led_strip_get_length(strip, &length) != ERROR_NONE || length == 0) {
        return;
    }

    led_strip_set_brightness(strip, 128); // About 50% brightness
    led_strip_fill_led_range_color(strip, 0, length, LED_RGB_COLOR_GREEN);
    led_strip_show(strip);

    led_strip_set_single_led_color(strip, 0, LED_RGB_COLOR_RED);
    led_strip_show(strip);

    led_strip_clear(strip);
}
```

## Using Literal RGB Values

Color macros are optional. Initialize `struct LedRgb` directly when a color is calculated or loaded at runtime. This example assumes the strip has at least four LEDs.

```c
#include <tactility/drivers/led_strip.h>

void show_custom_colors(struct Device *strip) {
    const struct LedRgb colors[] = {
        { .r = 255, .g = 32,  .b = 0   },
        { .r = 0,   .g = 180, .b = 64  },
        { .r = 24,  .g = 48,  .b = 255 },
        { .r = 200, .g = 80,  .b = 160 }
    };

    led_strip_set_led_range_color_and_brightness(strip, 0, 4, colors, 192);
    led_strip_show(strip);
}
```

## Color Presets

The kernel header also provides `LED_RGB_COLOR_*` macros such as `LED_RGB_COLOR_RED`, `LED_RGB_COLOR_GREEN`, and `LED_RGB_COLOR_WHITE`, plus `LED_COLOR_RGB_CUSTOM(r, g, b)` for a custom color. These expand to `struct LedRgb` values.

## Implementation Notes

- The driver configures the ESP-IDF RMT backend for WS2812 RGB output in GRB component order.
- An 8-bit gamma correction lookup table is applied after brightness scaling.
- The devicetree compatible string and driver registration are `"espressif,esp32-led-strip"`; the kernel device type is `LED_STRIP_TYPE`.
