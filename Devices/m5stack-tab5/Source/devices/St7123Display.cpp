#include "St7123Display.h"
#include "st7123_init_data.h"

#include <Tactility/Logger.h>
#include <esp_lcd_st7123.h>

static const auto LOGGER = tt::Logger("St7123");

St7123Display::~St7123Display() {
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

bool St7123Display::createMipiDsiBus() {
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
        .flags = {
            .adjustable = 0,
            .owned_by_hw = 0,
            .bypass = 0
        }
    };

    if (esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldoChannel) != ESP_OK) {
        LOGGER.error("Failed to acquire LDO channel for MIPI DSI PHY");
        return false;
    }

    LOGGER.info("Powered on");

    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 965  // ST7123 lane bitrate per M5Stack BSP
    };

    if (esp_lcd_new_dsi_bus(&bus_config, &mipiDsiBus) != ESP_OK) {
        LOGGER.error("Failed to create bus");
        esp_ldo_release_channel(ldoChannel);
        ldoChannel = nullptr;
        return false;
    }

    LOGGER.info("Bus created");
    return true;
}

bool St7123Display::createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) {
    if (mipiDsiBus == nullptr) {
        if (!createMipiDsiBus()) {
            return false;
        }
    }

    // DBI interface for LCD commands/parameters (8-bit cmd/param per ST7123 spec)
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    if (esp_lcd_new_panel_io_dbi(mipiDsiBus, &dbi_config, &ioHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel IO");
        esp_lcd_del_dsi_bus(mipiDsiBus);
        mipiDsiBus = nullptr;
        esp_ldo_release_channel(ldoChannel);
        ldoChannel = nullptr;
        return false;
    }

    return true;
}

esp_lcd_panel_dev_config_t St7123Display::createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) {
    return {
        .reset_gpio_num = resetPin,
        .rgb_ele_order = espLcdConfiguration->rgbElementOrder,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0
        },
        .vendor_config = nullptr
    };
}

bool St7123Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) {
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 70,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = 720,
            .v_size = 1280,
            .hsync_pulse_width = 2,
            .hsync_back_porch = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 8,
            .vsync_front_porch = 220,
        },
        .flags = {
            .use_dma2d = 1,
            .disable_lp = 0,
        }
    };

    st7123_vendor_config_t vendor_config = {
        .init_cmds = st7123_init_data,
        .init_cmds_size = std::size(st7123_init_data),
        .mipi_config = {
            .dsi_bus = mipiDsiBus,
            .dpi_config = &dpi_config,
        },
    };

    // Create a mutable copy of panelConfig to set vendor_config
    esp_lcd_panel_dev_config_t mutable_panel_config = panelConfig;
    mutable_panel_config.vendor_config = &vendor_config;

    if (esp_lcd_new_panel_st7123(ioHandle, &mutable_panel_config, &panelHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel");
        return false;
    }

    LOGGER.info("Panel created successfully");
    return true;
}

lvgl_port_display_dsi_cfg_t St7123Display::getLvglPortDisplayDsiConfig(esp_lcd_panel_io_handle_t /*ioHandle*/, esp_lcd_panel_handle_t /*panelHandle*/) {
    // Disable avoid_tearing to prevent stalls/blank flashes when other tasks (e.g. flash writes) block timing
    return lvgl_port_display_dsi_cfg_t{
        .flags = {
            .avoid_tearing = 0,
        },
    };
}
