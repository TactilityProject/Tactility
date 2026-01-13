#include "Ili9881cDisplay.h"
#include "disp_init_data.h"

#include <Tactility/Logger.h>
#include <esp_lcd_ili9881c.h>

static const auto LOGGER = tt::Logger("ILI9881C");

Ili9881cDisplay::~Ili9881cDisplay() {
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

bool Ili9881cDisplay::createMipiDsiBus() {
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

    // Create bus
    // TODO: use MIPI_DSI_PHY_CLK_SRC_DEFAULT() in future ESP-IDF 6.0.0 update with esp_lcd_jd9165 library version 2.x
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1000
    };

    if (esp_lcd_new_dsi_bus(&bus_config, &mipiDsiBus) != ESP_OK) {
        LOGGER.error("Failed to create bus");
        return false;
    }

    LOGGER.info("Bus created");
    return true;
}

bool Ili9881cDisplay::createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) {
    // Initialize MIPI DSI bus if not already done
    if (mipiDsiBus == nullptr) {
        if (!createMipiDsiBus()) {
            return false;
        }
    }

    // Use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = ILI9881C_PANEL_IO_DBI_CONFIG();

    if (esp_lcd_new_panel_io_dbi(mipiDsiBus, &dbi_config, &ioHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel IO");
        return false;
    }

    return true;
}

esp_lcd_panel_dev_config_t Ili9881cDisplay::createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) {
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

bool Ili9881cDisplay::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) {
    // Based on BSP: https://github.com/espressif/esp-bsp/blob/master/bsp/m5stack_tab5/README.md
    // TODO: undo static
    static esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1, // TODO: 2?
        .video_timing =
            {
                .h_size = 720,
                .v_size = 1280,
                .hsync_pulse_width = 40,
                .hsync_back_porch = 140,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 4,
                .vsync_back_porch = 20,
                .vsync_front_porch = 20,
            },
        .flags {
            .use_dma2d = 1, // TODO: true?
            .disable_lp = 0,
        }
    };

    // TODO: undo static
    static ili9881c_vendor_config_t vendor_config = {
        .init_cmds = disp_init_data,
        .init_cmds_size = std::size(disp_init_data),
        .mipi_config = {
            .dsi_bus = mipiDsiBus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };

    // Create a mutable copy of panelConfig to set vendor_config
    esp_lcd_panel_dev_config_t mutable_panel_config = panelConfig;
    mutable_panel_config.vendor_config = &vendor_config;

    if (esp_lcd_new_panel_ili9881c(ioHandle, &mutable_panel_config, &panelHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel");
        return false;
    }

    LOGGER.info("Panel created successfully");
    // Defer reset/init to base class applyConfiguration to avoid double initialization
    return true;
}

lvgl_port_display_dsi_cfg_t Ili9881cDisplay::getLvglPortDisplayDsiConfig(esp_lcd_panel_io_handle_t /*ioHandle*/, esp_lcd_panel_handle_t /*panelHandle*/) {
    // Disable avoid_tearing to prevent stalls/blank flashes when other tasks (e.g. flash writes) block timing
    return lvgl_port_display_dsi_cfg_t{
        .flags = {
            .avoid_tearing = 0,
        },
    };
}
