#include "Ssd1306Display.h"

#include <Tactility/Logger.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_dev.h>
#include <esp_lcd_panel_ssd1306.h>
#include <esp_lvgl_port.h>
#include <esp_lcd_panel_ops.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const auto LOGGER = tt::Logger("Ssd1306Display");

// SSD1306 commands
#define SSD1306_CMD_SET_CLOCK 0xD5
#define SSD1306_CMD_SET_CHARGE_PUMP 0x8D
#define SSD1306_CMD_SET_SEGMENT_REMAP 0xA0
#define SSD1306_CMD_SET_COM_SCAN_DIR 0xC0
#define SSD1306_CMD_SET_COM_PIN_CFG 0xDA
#define SSD1306_CMD_SET_CONTRAST 0x81
#define SSD1306_CMD_SET_PRECHARGE 0xD9
#define SSD1306_CMD_SET_VCOMH_DESELECT 0xDB
#define SSD1306_CMD_DISPLAY_NORMAL 0xA6
#define SSD1306_CMD_DISPLAY_INVERT 0xA7
#define SSD1306_CMD_DISPLAY_OFF 0xAE
#define SSD1306_CMD_DISPLAY_ON 0xAF
#define SSD1306_CMD_SET_MEMORY_MODE 0x20
#define SSD1306_CMD_SET_COLUMN_RANGE 0x21
#define SSD1306_CMD_SET_PAGE_RANGE 0x22
#define SSD1306_CMD_SET_MULTIPLEX 0xA8
#define SSD1306_CMD_SET_OFFSET 0xD3
#define SSD1306_CMD_STOP_SCROLL 0x2E
#define SSD1306_CMD_SET_SCAN_DIRECTION_REVERSED 0xC8

// Helper to send I2C commands directly
static bool ssd1306_i2c_send_cmd(i2c_port_t port, uint8_t addr, uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd}; // 0x00 = command mode
    esp_err_t ret = i2c_master_write_to_device(port, addr, data, sizeof(data), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        LOGGER.error("Failed to send command 0x{:02X}: {}", cmd, ret);
        return false;
    }
    return true;
}

static bool ssd1306_i2c_send(i2c_port_t port, uint8_t addr, uint16_t value) {
    esp_err_t ret = i2c_master_write_to_device(port, addr, reinterpret_cast<const uint8_t*>(&value), sizeof(value), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        LOGGER.error("Failed to send command 0x{:04X}: {}", value, ret);
        return false;
    }
    return true;
}

bool Ssd1306Display::createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) {
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = configuration->deviceAddress,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 0,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = false,
            .disable_control_phase = false,
        },
        .scl_speed_hz = 0
    };

    if (esp_lcd_new_panel_io_i2c(static_cast<esp_lcd_i2c_bus_handle_t>(configuration->port), &io_config, &ioHandle) != ESP_OK) {
        LOGGER.error("Failed to create IO handle");
        return false;
    }

    return true;
}

bool Ssd1306Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) {
    // Manual hardware reset with proper timing for Heltec V3
    if (configuration->resetPin != GPIO_NUM_NC) {
        gpio_config_t reset_gpio_config = {
            .pin_bit_mask = 1ULL << configuration->resetPin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&reset_gpio_config);
        
        gpio_set_level(configuration->resetPin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(configuration->resetPin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Create ESP-IDF panel (but don't call init - we'll do custom init)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC, // Already handled above
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 1, // Must be 1 for monochrome
        .flags = {
            .reset_active_high = false,
        },
        .vendor_config = nullptr,
    };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = static_cast<uint8_t>(configuration->verticalResolution),
    };
    panel_config.vendor_config = &ssd1306_config;
#endif

    if (esp_lcd_new_panel_ssd1306(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel");
        return false;
    }

    // Don't call esp_lcd_panel_init() - it doesn't configure correctly for Heltec V3!
    // Instead, send our custom initialization sequence directly via I2C
    
    auto port = configuration->port;
    auto addr = configuration->deviceAddress;
    
    LOGGER.info("Sending Heltec V3 custom init sequence");

    // Display off while configuring
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_DISPLAY_OFF);

    // Set oscillator frequency (MUST come early in sequence)
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_CLOCK);
    ssd1306_i2c_send_cmd(port, addr, 0xF0); // ~96 Hz

    // Set multiplex ratio
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_MULTIPLEX);
    ssd1306_i2c_send_cmd(port, addr, configuration->verticalResolution - 1);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_OFFSET);
    ssd1306_i2c_send_cmd(port, addr, 0x00);
    ssd1306_i2c_send_cmd(port, addr, 0x40);

    // Enable charge pump (required for Heltec V3 - must be before memory mode)
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_CHARGE_PUMP);
    ssd1306_i2c_send_cmd(port, addr, 0x14); // Enable

    // Horizontal addressing mode
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_MEMORY_MODE);
    ssd1306_i2c_send_cmd(port, addr, 0x00); // Horizontal addressing

    // Segment remap (0xA1 for Heltec V3 orientation)
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_SEGMENT_REMAP | 0x01);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_SCAN_DIRECTION_REVERSED );

    // COM pin configuration
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_COM_PIN_CFG);
    if (configuration->verticalResolution == 64) {
        ssd1306_i2c_send_cmd(port, addr, 0x12); // Alternative COM pin config for 64-row displays
    } else {
        ssd1306_i2c_send_cmd(port, addr, 0x02); // Sequential COM pin config for 32-row displays
    }

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_CONTRAST);
    ssd1306_i2c_send_cmd(port, addr, 0xCF);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_PRECHARGE);
    ssd1306_i2c_send_cmd(port, addr, 0xF1);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_VCOMH_DESELECT);
    ssd1306_i2c_send_cmd(port, addr, 0x40);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_COLUMN_RANGE);
    ssd1306_i2c_send_cmd(port, addr, 0);
    ssd1306_i2c_send_cmd(port, addr, configuration->horizontalResolution - 1);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_SET_PAGE_RANGE);
    ssd1306_i2c_send_cmd(port, addr, 0);
    if (configuration->verticalResolution == 64)
        ssd1306_i2c_send_cmd(port, addr, 0x7);
    else if (configuration->verticalResolution == 32)
        ssd1306_i2c_send_cmd(port, addr, 0x3);

    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_DISPLAY_INVERT);
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_STOP_SCROLL);
    ssd1306_i2c_send_cmd(port, addr, SSD1306_CMD_DISPLAY_ON);

    vTaskDelay(pdMS_TO_TICKS(100)); // Let display stabilize

    LOGGER.info("Heltec V3 display initialized successfully");

    return true;
}

lvgl_port_display_cfg_t Ssd1306Display::getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
    return {
        .io_handle = ioHandle,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = configuration->bufferSize,
        .double_buffer = false,
        .trans_size = 0,
        .hres = configuration->horizontalResolution,
        .vres = configuration->verticalResolution,
        .monochrome = true, // ESP-LVGL-port handles the conversion!
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565, // Use RGB565, monochrome flag makes it work!
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = true,
            .direct_mode = false
        }
    };
}
