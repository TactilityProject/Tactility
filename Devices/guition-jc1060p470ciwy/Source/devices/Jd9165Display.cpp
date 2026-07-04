#include "Jd9165Display.h"

#include <tactility/log.h>

#include <esp_lcd_jd9165.h>

constexpr auto* TAG = "JD9165";

// MIPI DSI PHY power configuration
#define MIPI_DSI_PHY_PWR_LDO_CHAN 3  // LDO_VO3 connects to VDD_MIPI_DPHY
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

// JD9165 initialization commands from ESP32-P4 Function EV Board
// Delays set to match the reference sequence exactly.
static const jd9165_lcd_init_cmd_t jd9165_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49,0x61,0x02,0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A,0x1A,0x0B,0x0D,0x0D,0x11,0x10,0x06,0x08,0x1F,0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16,0x1B,0x0B,0x0D,0x0D,0x11,0x10,0x07,0x09,0x1E,0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16,0x1B,0x0D,0x0B,0x0D,0x11,0x10,0x1C,0x1E,0x09,0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A,0x1A,0x0D,0x0B,0x0D,0x11,0x10,0x1D,0x1F,0x08,0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00,0x00,0x11,0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0X3A, (uint8_t[]){0x55}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

Jd9165Display::~Jd9165Display() {
    // TODO: This should happen during ::stop(), but this isn't currently exposed
    if (mipiDsiBus != nullptr) {
        esp_lcd_del_dsi_bus(mipiDsiBus);
        mipiDsiBus = nullptr;
    }
    if (ldoChannel != nullptr) {
        esp_ldo_release_channel(ldoChannel);
        ldoChannel = nullptr;
    }
}

bool Jd9165Display::createMipiDsiBus() {
    // Enable MIPI DSI PHY power (transition from "no power" to "shutdown" state)
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        .flags = {}
    };
    
    if (esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldoChannel) != ESP_OK) {
        LOG_E(TAG, "Failed to acquire LDO channel for MIPI DSI PHY");
        return false;
    }

    LOG_I(TAG, "MIPI DSI PHY powered on");

    // Create MIPI DSI bus
    // TODO: use MIPI_DSI_PHY_CLK_SRC_DEFAULT() in future ESP-IDF 6.0.0 update with esp_lcd_jd9165 library version 2.x
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 750
    };

    if (esp_lcd_new_dsi_bus(&bus_config, &mipiDsiBus) != ESP_OK) {
        LOG_E(TAG, "Failed to create MIPI DSI bus");
        return false;
    }

    LOG_I(TAG, "MIPI DSI bus created");
    return true;
}

bool Jd9165Display::createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) {
    // Initialize MIPI DSI bus if not already done
    if (mipiDsiBus == nullptr) {
        if (!createMipiDsiBus()) {
            return false;
        }
    }

    // Use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = JD9165_PANEL_IO_DBI_CONFIG();

    if (esp_lcd_new_panel_io_dbi(mipiDsiBus, &dbi_config, &ioHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO");
        return false;
    }

    return true;
}

esp_lcd_panel_dev_config_t Jd9165Display::createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) {
    return {
        .reset_gpio_num = resetPin,
        .rgb_ele_order = espLcdConfiguration->rgbElementOrder,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = static_cast<uint8_t>(espLcdConfiguration->bitsPerPixel),
        .flags = {
            .reset_active_high = 0
        },
        .vendor_config = nullptr  // Will be set in createPanelHandle
    };
}

bool Jd9165Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) {
    // Create DPI panel configuration
    // Override default timings
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 50,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = 1024,
            .v_size = 600,
            .hsync_pulse_width = 20,
            .hsync_back_porch = 160,
            .hsync_front_porch = 160,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 21,
            .vsync_front_porch = 12,
        },
        .flags = {
            .use_dma2d = 1,
            .disable_lp = 0
        }
    };

    jd9165_vendor_config_t vendor_config = {
        .init_cmds = jd9165_init_cmds,
        .init_cmds_size = sizeof(jd9165_init_cmds) / sizeof(jd9165_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = mipiDsiBus,
            .dpi_config = &dpi_config,
        },
    };

    // Create a mutable copy of panelConfig to set vendor_config
    esp_lcd_panel_dev_config_t mutable_panel_config = panelConfig;
    mutable_panel_config.vendor_config = &vendor_config;

    if (esp_lcd_new_panel_jd9165(ioHandle, &mutable_panel_config, &panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        return false;
    }

    LOG_I(TAG, "JD9165 panel created successfully");
    // Defer reset/init to base class applyConfiguration to avoid double initialization
    return true;
}

lvgl_port_display_dsi_cfg_t Jd9165Display::getLvglPortDisplayDsiConfig(esp_lcd_panel_io_handle_t /*ioHandle*/, esp_lcd_panel_handle_t /*panelHandle*/) {
    // Disable avoid_tearing to prevent stalls/blank flashes when other tasks (e.g. flash writes) block timing
    return lvgl_port_display_dsi_cfg_t{
        .flags = {
            .avoid_tearing = 0,
        },
    };
}
