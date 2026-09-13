#ifndef _EPAPER_PANEL_H_
#define _EPAPER_PANEL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "epaper_config.h"

// Forward declaration
typedef struct epd_device epd_device_t;

/*=============================================================================
 * Capability Flags
 *============================================================================*/
#define EPD_CAP_PARTIAL     (1 << 0)  // Supports partial refresh
#define EPD_CAP_FAST        (1 << 1)  // Supports fast refresh
#define EPD_CAP_GRAYSCALE   (1 << 2)  // Supports grayscale mode
#define EPD_CAP_BUSY_INV    (1 << 3)  // Inverted busy signal (HIGH=ready)

/*=============================================================================
 * Controller Types
 *============================================================================*/
typedef enum {
    EPD_CTRL_SSD16XX,       // SSD1680/1681 BW panels (generic)
    EPD_CTRL_GDEY0154_LUT,  // GDEY0154D67 with custom LUT
    EPD_CTRL_ACEP_6COLOR,   // ACeP 6-color controller (GDEP073E01)
    EPD_CTRL_BWRY_4COLOR,   // BWRY 4-color controller (GDEY037F51)
    EPD_CTRL_COUNT
} epd_controller_type_t;

/*=============================================================================
 * Panel Descriptor (Data-Driven)
 *============================================================================*/
typedef struct {
    const char *name;               // Panel name for logging
    uint16_t width;                 // Default width in pixels
    uint16_t height;                // Default height in pixels
    epd_color_mode_t color_mode;    // Color mode
    uint8_t bits_per_pixel;         // 1, 2, or 4
    uint32_t caps;                  // Capability flags (EPD_CAP_*)
    epd_controller_type_t ctrl;     // Controller type
    uint8_t source_shift_bytes;     // GDDRAM X window offset in bytes (GDEY029T71H: 1)
    bool fast_full_update;          // SSD1685 fast full update: bypass RED + temp override
    bool ssd1685_partial;           // SSD1685 partial: 0x21=(0x00,0x00) + 0x22=0xDC
    uint8_t full_refresh_interval;  // Periodic full refresh after N partials (0 = disabled)
    const void *init_data;          // Panel-specific init data (LUT, etc.)
} epd_panel_desc_t;

/*=============================================================================
 * Controller Operations Interface
 *============================================================================*/
typedef struct {
    esp_err_t (*init)(epd_device_t *dev);
    esp_err_t (*update)(epd_device_t *dev, epd_update_mode_t mode);
    esp_err_t (*write_ram)(epd_device_t *dev, const uint8_t *data, uint32_t len);
    esp_err_t (*write_ram_partial)(epd_device_t *dev, uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h, const uint8_t *data);
    esp_err_t (*sleep)(epd_device_t *dev);
    esp_err_t (*wake)(epd_device_t *dev);
} epd_controller_ops_t;

/*=============================================================================
 * Registry Functions
 *============================================================================*/

// Get panel descriptor by type
const epd_panel_desc_t* epd_get_panel_desc(epd_panel_type_t type);

// Get controller operations by type
const epd_controller_ops_t* epd_get_controller_ops(epd_controller_type_t ctrl);

// Check if panel supports a capability
static inline bool epd_panel_has_cap(const epd_panel_desc_t *panel, uint32_t cap) {
    return panel && (panel->caps & cap);
}

// Calculate buffer size for panel
static inline uint32_t epd_calc_buffer_size(uint16_t w, uint16_t h, uint8_t bpp) {
    return ((uint32_t)w * h * bpp + 7) / 8;
}

#endif // _EPAPER_PANEL_H_
