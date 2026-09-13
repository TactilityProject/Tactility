# Adding a New Panel

This guide explains how to add support for a new e-paper panel using the data-driven architecture.

## Architecture Overview

```
epaper_config.h          epaper_registry.c         controllers/
+----------------+       +-------------------+      +----------------+
| EPD_PANEL_xxx  | ----> | panel_registry[]  | ---> | ssd16xx.c      |
| (enum)         |       | - name            |      | gdey0154_lut.c |
+----------------+       | - width/height    |      | acep_6color.c  |
                         | - color_mode      |      | bwry_4color.c  |
                         | - bits_per_pixel  |      +----------------+
                         | - caps            |
                         | - ctrl (enum)     | ---> controller_ops[]
                         +-------------------+      (vtable lookup)
```

## Quick Start: Same Controller, Different Size

**For BW panels using existing SSD16xx controller** (covers 90% of GoodDisplay panels), adding support requires only **1 line of code**:

### Step 1: Add Panel Type Enum

In `include/epaper_config.h`:

```c
typedef enum {
    // ... existing panels ...
    EPD_PANEL_SSD16XX_290,      // 2.9" BW 128x296
    EPD_PANEL_SSD16XX_YOUR_NEW, // <-- Add here
    EPD_PANEL_SSD16XX_420,      // 4.2" BW 400x300

    EPD_PANEL_COUNT,            // Must be BEFORE aliases!
} epd_panel_type_t;
```

### Step 2: Add to Panel Registry

In `src/epaper_registry.c`, add one line to `panel_registry[]`:

```c
static const epd_panel_desc_t panel_registry[EPD_PANEL_COUNT] = {
    // ... existing panels ...

    [EPD_PANEL_SSD16XX_YOUR_NEW] = {
        .name = "SSD16xx_YOUR",
        .width = YOUR_WIDTH,
        .height = YOUR_HEIGHT,
        .color_mode = EPD_COLOR_BW,
        .bits_per_pixel = 1,
        .caps = EPD_CAP_PARTIAL | EPD_CAP_FAST,
        .ctrl = EPD_CTRL_SSD16XX,    // Reuse existing controller
    },
};
```

**That's it!** The existing `EPD_CTRL_SSD16XX` controller handles everything.

### Currently Supported Generic Panels

| Panel Type | Size | Resolution | Notes |
|------------|------|------------|-------|
| `EPD_PANEL_SSD16XX_154` | 1.54" | 200x200 | GDEM0154I61, generic 1.54" |
| `EPD_PANEL_SSD16XX_213` | 2.13" | 122x250 | GDEY0213B74, GDEM0213I61 |
| `EPD_PANEL_SSD16XX_266` | 2.66" | 152x296 | GDEY0266T90, GDEY0266T90H |
| `EPD_PANEL_SSD16XX_270` | 2.7" | 176x264 | GDEY027T91, GDEM027Q72 |
| `EPD_PANEL_SSD16XX_290` | 2.9" | 128x296 | GDEY029T94, GDEY029T71H |
| `EPD_PANEL_SSD16XX_370` | 3.7" | 280x480 | GDEY037T03 |
| `EPD_PANEL_SSD16XX_420` | 4.2" | 400x300 | GDEY042T81, GDEQ0426T82 |

---

## Adding a New Controller

For panels with **custom LUT tables**, **different controllers**, or **special features**, you need to add a new controller.

### Step 1: Create Controller File

Create `src/controllers/your_controller.c`:

```c
/**
 * @file your_controller.c
 * @brief Driver for YOUR_CONTROLLER e-paper displays
 */

#include "../epaper_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "your_ctrl";

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t your_controller_init(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    uint16_t w = epd_get_width(dev);
    uint16_t h = epd_get_height(dev);

    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(50));
    WAIT(dev);  // Uses polarity from panel caps

    CMD(spi, 0x12);  // SWRESET
    WAIT(dev);

    CMD(spi, 0x01);  // Driver output control
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, ((h - 1) >> 8) & 0xFF);
    DATA(spi, 0x00);

    CMD(spi, 0x11);  // Data entry mode
    DATA(spi, 0x03);

    // Set RAM address
    CMD(spi, 0x44);
    DATA(spi, 0x00);
    DATA(spi, (w / 8) - 1);

    CMD(spi, 0x45);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, ((h - 1) >> 8) & 0xFF);

    WAIT(dev);

    ESP_LOGI(TAG, "Panel initialized: %dx%d", w, h);
    return ESP_OK;
}

/*=============================================================================
 * Update Functions
 *============================================================================*/

esp_err_t your_controller_update(epd_device_t *dev, epd_update_mode_t mode)
{
    epd_spi_t *spi = epd_get_spi(dev);

    switch (mode) {
        case EPD_UPDATE_PARTIAL:
            CMD(spi, 0x22);
            DATA(spi, 0xFF);  // Partial update sequence
            break;
        case EPD_UPDATE_FAST:
            CMD(spi, 0x22);
            DATA(spi, 0xC7);  // Fast update
            break;
        case EPD_UPDATE_FULL:
        default:
            CMD(spi, 0x22);
            DATA(spi, 0xF7);  // Full update
            break;
    }

    CMD(spi, 0x20);  // Master activation
    WAIT(dev);

    return ESP_OK;
}

/*=============================================================================
 * RAM Write Functions
 *============================================================================*/

esp_err_t your_controller_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    epd_spi_t *spi = epd_get_spi(dev);
    bool partial_ready = epd_is_partial_ready(dev);

    // Reset RAM address
    CMD(spi, 0x4E);
    DATA(spi, 0x00);
    CMD(spi, 0x4F);
    DATA(spi, 0x00);
    DATA(spi, 0x00);

    // Write to current RAM (0x24)
    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, len);

    // Only write to base RAM (0x26) when setting up partial refresh
    if (!partial_ready) {
        CMD(spi, 0x4E);
        DATA(spi, 0x00);
        CMD(spi, 0x4F);
        DATA(spi, 0x00);
        DATA(spi, 0x00);

        CMD(spi, 0x26);
        epd_spi_write_data_bulk(spi, data, len);
    }

    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t your_controller_sleep(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x10);  // Deep sleep
    DATA(spi, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Panel in deep sleep");
    return ESP_OK;
}

esp_err_t your_controller_wake(epd_device_t *dev)
{
    return your_controller_init(dev);
}
```

### Step 2: Add Controller Type Enum

In `include/epaper_panel.h`:

```c
typedef enum {
    EPD_CTRL_SSD16XX,
    EPD_CTRL_GDEY0154_LUT,
    EPD_CTRL_ACEP_6COLOR,
    EPD_CTRL_BWRY_4COLOR,
    EPD_CTRL_YOUR_CONTROLLER,  // <-- Add here
    EPD_CTRL_COUNT
} epd_controller_type_t;
```

### Step 3: Register Controller Operations

In `src/epaper_registry.c`:

```c
// Declare functions
extern esp_err_t your_controller_init(epd_device_t *dev);
extern esp_err_t your_controller_update(epd_device_t *dev, epd_update_mode_t mode);
extern esp_err_t your_controller_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len);
extern esp_err_t your_controller_sleep(epd_device_t *dev);
extern esp_err_t your_controller_wake(epd_device_t *dev);

// Add to controller_ops array
static const epd_controller_ops_t controller_ops[EPD_CTRL_COUNT] = {
    // ... existing controllers ...

    [EPD_CTRL_YOUR_CONTROLLER] = {
        .init = your_controller_init,
        .update = your_controller_update,
        .write_ram = your_controller_write_ram,
        .write_ram_partial = NULL,  // Optional
        .sleep = your_controller_sleep,
        .wake = your_controller_wake,
    },
};
```

### Step 4: Add Panel to Registry

In `src/epaper_registry.c`:

```c
static const epd_panel_desc_t panel_registry[EPD_PANEL_COUNT] = {
    // ... existing panels ...

    [EPD_PANEL_YOUR_PANEL] = {
        .name = "YOUR_PANEL",
        .width = 200,
        .height = 200,
        .color_mode = EPD_COLOR_BW,
        .bits_per_pixel = 1,
        .caps = EPD_CAP_PARTIAL | EPD_CAP_FAST,
        .ctrl = EPD_CTRL_YOUR_CONTROLLER,
    },
};
```

### Step 5: Update CMakeLists.txt

```cmake
idf_component_register(
    SRCS
        "src/epaper.c"
        "src/epaper_spi.c"
        "src/epaper_lvgl.c"
        "src/epaper_common.c"
        "src/epaper_registry.c"
        "src/controllers/ssd16xx.c"
        "src/controllers/gdey0154_lut.c"
        "src/controllers/acep_6color.c"
        "src/controllers/bwry_4color.c"
        "src/controllers/your_controller.c"  # <-- Add here
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_driver_spi esp_timer
    PRIV_REQUIRES lvgl esp_psram
)
```

---

## Panel Descriptor Reference

```c
typedef struct {
    const char *name;               // Panel name for logging
    uint16_t width;                 // Width in pixels
    uint16_t height;                // Height in pixels
    epd_color_mode_t color_mode;    // EPD_COLOR_BW, EPD_COLOR_6COLOR, etc.
    uint8_t bits_per_pixel;         // 1, 2, or 4
    uint32_t caps;                  // EPD_CAP_* flags
    epd_controller_type_t ctrl;     // Controller type
    const void *init_data;          // Optional panel-specific data
} epd_panel_desc_t;
```

### Capability Flags

| Flag | Description |
|------|-------------|
| `EPD_CAP_PARTIAL` | Supports partial refresh |
| `EPD_CAP_FAST` | Supports fast refresh mode |
| `EPD_CAP_GRAYSCALE` | Supports grayscale mode |
| `EPD_CAP_BUSY_INV` | Inverted busy signal (HIGH = ready) |

### Color Modes

| Mode | BPP | Buffer Size | Description |
|------|-----|-------------|-------------|
| `EPD_COLOR_BW` | 1 | W*H/8 | Black/White |
| `EPD_COLOR_BWR` | 2 | W*H/4 | Black/White/Red |
| `EPD_COLOR_BWY` | 2 | W*H/4 | Black/White/Yellow |
| `EPD_COLOR_4GRAY` | 2 | W*H/4 | 4 Gray levels |
| `EPD_COLOR_4COLOR` | 2 | W*H/4 | 4-color BWRY |
| `EPD_COLOR_6COLOR` | 4 | W*H/2 | 6 Colors |
| `EPD_COLOR_7COLOR` | 4 | W*H/2 | 7 Colors |

---

## Controller Operations Interface

```c
typedef struct {
    esp_err_t (*init)(epd_device_t *dev);
    esp_err_t (*update)(epd_device_t *dev, epd_update_mode_t mode);
    esp_err_t (*write_ram)(epd_device_t *dev, const uint8_t *data, uint32_t len);
    esp_err_t (*write_ram_partial)(epd_device_t *dev, uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h, const uint8_t *data);
    esp_err_t (*sleep)(epd_device_t *dev);
    esp_err_t (*wake)(epd_device_t *dev);
} epd_controller_ops_t;
```

| Function | Required | Description |
|----------|----------|-------------|
| `init` | Yes | Initialize panel hardware |
| `update` | Yes | Trigger display refresh (handles all modes) |
| `write_ram` | Yes | Write framebuffer to display RAM |
| `write_ram_partial` | No | Windowed RAM write for partial regions |
| `sleep` | No | Enter deep sleep mode |
| `wake` | No | Wake from sleep (usually calls init) |

---

## Helper Functions & Macros

Available in `epaper_common.h`:

```c
// Device access
epd_spi_t* epd_get_spi(epd_device_t *dev);
uint16_t epd_get_width(epd_device_t *dev);
uint16_t epd_get_height(epd_device_t *dev);
const epd_panel_desc_t* epd_get_panel(epd_device_t *dev);
bool epd_is_partial_ready(epd_device_t *dev);

// SPI macros
#define CMD(spi, c)     epd_spi_write_cmd(spi, c)
#define DATA(spi, d)    epd_spi_write_data(spi, d)
#define RESET(spi)      epd_spi_reset(spi)

// Busy wait with polarity support
#define WAIT(dev) epd_wait_busy_polarity(epd_get_spi(dev), epd_get_panel(dev)->caps, 0)
```

---

## Common Issues and Solutions

### Busy Signal Polarity

Some panels use inverted busy signal. Add `EPD_CAP_BUSY_INV` to capabilities:

```c
// Standard: LOW = ready, HIGH = busy
.caps = EPD_CAP_PARTIAL,

// Inverted: HIGH = ready, LOW = busy (e.g., GDEP073E01)
.caps = EPD_CAP_BUSY_INV,
```

The `WAIT(dev)` macro automatically handles polarity based on panel caps.

### Partial Refresh RAM Management

For partial refresh to work correctly:

1. **First call** (`partial_ready = false`): Write to both RAM 0x24 and 0x26
2. **Subsequent calls** (`partial_ready = true`): Write only to RAM 0x24

Use `epd_is_partial_ready(dev)` to check the state:

```c
esp_err_t your_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    bool partial_ready = epd_is_partial_ready(dev);

    // Always write to current RAM
    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, len);

    // Only write to base RAM when setting up partial mode
    if (!partial_ready) {
        CMD(spi, 0x26);
        epd_spi_write_data_bulk(spi, data, len);
    }

    return ESP_OK;
}
```

### Multi-Color Panel RAM Format

6/7-color panels use 4-bit per pixel (2 pixels per byte):

```c
// High nibble = even pixel, Low nibble = odd pixel
static void set_pixel_4bpp(uint8_t *fb, int x, int y, int width, uint8_t color)
{
    uint32_t addr = (y * width + x) / 2;
    if (x % 2 == 0) {
        fb[addr] = (fb[addr] & 0x0F) | (color << 4);
    } else {
        fb[addr] = (fb[addr] & 0xF0) | (color & 0x0F);
    }
}
```

4-color BWRY panels use 2-bit per pixel (4 pixels per byte):

```c
// Pixel order: [P0:7-6][P1:5-4][P2:3-2][P3:1-0]
static void set_pixel_2bpp(uint8_t *fb, int x, int y, int width, uint8_t color)
{
    uint32_t pixel_idx = y * width + x;
    uint32_t byte_idx = pixel_idx / 4;
    uint8_t bit_offset = (3 - (pixel_idx % 4)) * 2;  // 6, 4, 2, 0
    uint8_t mask = 0x03 << bit_offset;
    fb[byte_idx] = (fb[byte_idx] & ~mask) | ((color & 0x03) << bit_offset);
}
```

---

## Adding Board Preset (Optional)

Create a configuration macro for easy setup in `include/epaper_config.h`:

```c
#define EPD_CONFIG_YOUR_BOARD() { \
    .pins = { \
        .busy = GPIO_NUM_8, \
        .rst = GPIO_NUM_9, \
        .dc = GPIO_NUM_10, \
        .cs = GPIO_NUM_11, \
        .sck = GPIO_NUM_12, \
        .mosi = GPIO_NUM_13, \
    }, \
    .spi = { \
        .host = SPI2_HOST, \
        .speed_hz = 10000000, \
    }, \
    .panel = { \
        .type = EPD_PANEL_YOUR_PANEL, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}
```

---

## Testing

1. **Build test**: `idf.py build`
2. **Basic test**: Fill screen with white, then black
3. **Pattern test**: Draw checkerboard or gradient
4. **Partial test**: Update small region repeatedly
5. **Sleep/wake test**: Verify low power mode

---

## Resources

- [Good Display Wiki](https://www.good-display.com/companyfile/101.html) - Datasheets and app notes
- [Waveshare E-Paper Wiki](https://www.waveshare.com/wiki/E-Paper) - Reference implementations
- [ESP-IDF SPI Master](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html)
