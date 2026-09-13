#ifndef _EPAPER_CONFIG_H_
#define _EPAPER_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

// Default pinout (ESP32-WROOM-32D)
#define EPD_DEFAULT_PIN_BUSY    14
#define EPD_DEFAULT_PIN_RST     15
#define EPD_DEFAULT_PIN_DC      16
#define EPD_DEFAULT_PIN_CS      17
#define EPD_DEFAULT_PIN_SCK     18
#define EPD_DEFAULT_PIN_MOSI    23

#define EPD_DEFAULT_SPI_HOST    SPI2_HOST
#define EPD_DEFAULT_SPI_SPEED   4000000  // 4MHz

// Panel types
typedef enum {
    // Specific panels (with custom LUT or special features)
    EPD_PANEL_GDEY0154D67 = 0,  // 1.54" BW 200x200 (custom LUT for better partial)
    EPD_PANEL_GDEP073E01,       // 7.3" 6-Color 800x480
    EPD_PANEL_GDEY037F51,       // 3.7" 4-Color BWRY 240x416
    EPD_PANEL_GDEY029T71H,      // 2.9" BW 168x384 (SSD1685, sources S8..S175)

    // Generic SSD16xx BW panels (same driver, different sizes)
    EPD_PANEL_SSD16XX_154,      // 1.54" BW 200x200 (generic, no custom LUT)
    EPD_PANEL_SSD16XX_213,      // 2.13" BW 122x250 (GDEY0213B74, etc.)
    EPD_PANEL_SSD16XX_266,      // 2.66" BW 152x296 (GDEY0266T90, etc.)
    EPD_PANEL_SSD16XX_270,      // 2.7" BW 176x264 (GDEY027T91, etc.)
    EPD_PANEL_SSD16XX_290,      // 2.9" BW 128x296 (GDEY029T94, etc.)
    EPD_PANEL_SSD16XX_370,      // 3.7" BW 280x480 (GDEY037T03, etc.)
    EPD_PANEL_SSD16XX_420,      // 4.2" BW 400x300 (GDEY042T81, etc.)

    EPD_PANEL_COUNT,            // Must be BEFORE aliases!

    // Legacy aliases (for backward compatibility, don't affect count)
    EPD_PANEL_GDEY0266T90 = EPD_PANEL_SSD16XX_266,
} epd_panel_type_t;

// Board presets
typedef enum {
    EPD_BOARD_CUSTOM = 0,
    EPD_BOARD_ESP32_WROOM,        // Default ESP32-WROOM-32D
    EPD_BOARD_ESP32S3_EPAPER_154, // ESP32-S3-ePaper-1.54 board
    EPD_BOARD_ESP32S3_EPAPER_73,  // ESP32-S3 + 7.3" 6-Color board
} epd_board_preset_t;

// Color mode
typedef enum {
    EPD_COLOR_BW = 0,           // Black/White (1-bit)
    EPD_COLOR_BWR,              // Black/White/Red (3-color)
    EPD_COLOR_BWY,              // Black/White/Yellow (3-color)
    EPD_COLOR_4GRAY,            // 4 Gray levels
    EPD_COLOR_4COLOR,           // 4-color BWRY (Black/White/Yellow/Red, 2-bit)
    EPD_COLOR_7COLOR,           // 7-color (B/W/R/Y/Orange/Green/Blue)
    EPD_COLOR_6COLOR,           // 6-color (B/W/R/Y/Green/Blue)
} epd_color_mode_t;

// E-paper color indices (for multi-color panels)
#define EPD_PIXEL_BLACK     0x0
#define EPD_PIXEL_WHITE     0x1
#define EPD_PIXEL_YELLOW    0x2
#define EPD_PIXEL_RED       0x3
#define EPD_PIXEL_ORANGE    0x4
#define EPD_PIXEL_BLUE      0x5
#define EPD_PIXEL_GREEN     0x6

// Dithering mode
typedef enum {
    EPD_DITHER_NONE = 0,        // No dithering (direct color mapping)
    EPD_DITHER_FLOYD_STEINBERG, // Floyd-Steinberg error diffusion
    EPD_DITHER_ORDERED,         // Ordered/Bayer dithering
} epd_dither_mode_t;

// Update mode
typedef enum {
    EPD_UPDATE_FULL = 0,        // Full refresh
    EPD_UPDATE_FAST,            // Fast refresh
    EPD_UPDATE_PARTIAL,         // Partial refresh
} epd_update_mode_t;

// Pin configuration
typedef struct {
    int busy;
    int rst;
    int dc;
    int cs;
    int sck;
    int mosi;
} epd_pin_config_t;

// SPI configuration
typedef struct {
    int host;                   // SPI host (SPI2_HOST, SPI3_HOST)
    int speed_hz;               // Clock speed
} epd_spi_config_t;

// Panel configuration
typedef struct {
    epd_panel_type_t type;
    uint16_t width;             // Override width (0 = use default)
    uint16_t height;            // Override height (0 = use default)
    bool mirror_x;
    bool mirror_y;
    uint8_t rotation;           // 0, 90, 180, 270
} epd_panel_config_t;

// Full device configuration
typedef struct {
    epd_pin_config_t pins;
    epd_spi_config_t spi;
    epd_panel_config_t panel;
} epd_config_t;

// Default configuration macro (ESP32-WROOM-32D)
#define EPD_CONFIG_DEFAULT() { \
    .pins = { \
        .busy = EPD_DEFAULT_PIN_BUSY, \
        .rst = EPD_DEFAULT_PIN_RST, \
        .dc = EPD_DEFAULT_PIN_DC, \
        .cs = EPD_DEFAULT_PIN_CS, \
        .sck = EPD_DEFAULT_PIN_SCK, \
        .mosi = EPD_DEFAULT_PIN_MOSI, \
    }, \
    .spi = { \
        .host = EPD_DEFAULT_SPI_HOST, \
        .speed_hz = EPD_DEFAULT_SPI_SPEED, \
    }, \
    .panel = { \
        .type = EPD_PANEL_GDEY0266T90, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}

// ESP32-S3-ePaper-1.54 board configuration
// Pinout: DC=10, CS=11, SCK=12, MOSI=13, RST=9, BUSY=8
#define EPD_CONFIG_ESP32S3_154() { \
    .pins = { \
        .busy = 8, \
        .rst = 9, \
        .dc = 10, \
        .cs = 11, \
        .sck = 12, \
        .mosi = 13, \
    }, \
    .spi = { \
        .host = SPI2_HOST, \
        .speed_hz = 40000000, \
    }, \
    .panel = { \
        .type = EPD_PANEL_GDEY0154D67, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}

// 7.3" 6-Color E-Paper configuration (800x480)
// Adjust pins according to your board
#define EPD_CONFIG_73_6COLOR() { \
    .pins = { \
        .busy = 13, \
        .rst = 12, \
        .dc = 8, \
        .cs = 9, \
        .sck = 10, \
        .mosi = 11, \
    }, \
    .spi = { \
        .host = SPI2_HOST, \
        .speed_hz = 10000000, \
    }, \
    .panel = { \
        .type = EPD_PANEL_GDEP073E01, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}

// Good Display ESP32-WROOM-32D development board
// Default panel: 2.66" BW (152x296)
// Pinout: BUSY=13, RST=12, DC=14, CS=27, SCK=18, MOSI=23
#define EPD_CONFIG_ESP32_WROOM() { \
    .pins = { \
        .busy = 13, \
        .rst = 12, \
        .dc = 14, \
        .cs = 27, \
        .sck = 18, \
        .mosi = 23, \
    }, \
    .spi = { \
        .host = SPI2_HOST, \
        .speed_hz = 10000000, \
    }, \
    .panel = { \
        .type = EPD_PANEL_SSD16XX_266, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}

// Good Display ESP32-WROOM-32D + 3.7" 4-Color BWRY (GDEY037F51)
// 240x416 resolution, no PSRAM required (streaming mode)
// Pinout: BUSY=13, RST=12, DC=14, CS=27, SCK=18, MOSI=23
#define EPD_CONFIG_ESP32_WROOM_4COLOR() { \
    .pins = { \
        .busy = 13, \
        .rst = 12, \
        .dc = 14, \
        .cs = 27, \
        .sck = 18, \
        .mosi = 23, \
    }, \
    .spi = { \
        .host = SPI2_HOST, \
        .speed_hz = 10000000, \
    }, \
    .panel = { \
        .type = EPD_PANEL_GDEY037F51, \
        .width = 0, \
        .height = 0, \
        .mirror_x = false, \
        .mirror_y = false, \
        .rotation = 0, \
    }, \
}

#endif // _EPAPER_CONFIG_H_
