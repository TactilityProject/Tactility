#include "Ili9488Display.h"

#include <tactility/log.h>

#include <esp_lcd_ili9488.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lvgl_port.h>

constexpr auto* TAG = "ILI9488";

bool Ili9488Display::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    const esp_lcd_panel_io_spi_config_t panel_io_config = {
        .cs_gpio_num = configuration->csPin,
        .dc_gpio_num = configuration->dcPin,
        .spi_mode = 0,
        .pclk_hz = configuration->pixelClockFrequency,
        .trans_queue_depth = configuration->transactionQueueDepth,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_high_on_cmd = 0,
            .dc_low_on_data = 0,
            .dc_low_on_param = 0,
            .octal_mode = 0,
            .quad_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0
        }
    };

    return esp_lcd_new_panel_io_spi(configuration->spiHostDevice, &panel_io_config, &outHandle) == ESP_OK;
}

bool Ili9488Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) {

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration->resetPin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 18,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = nullptr
    };

    if (esp_lcd_new_panel_ili9488(ioHandle, &panel_config, configuration->bufferSize, &panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        return false;
    }

    if (esp_lcd_panel_reset(panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to reset panel");
        return false;
    }

    if (esp_lcd_panel_init(panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to init panel");
        return false;
    }

    if (esp_lcd_panel_swap_xy(panelHandle, configuration->swapXY) != ESP_OK) {
        LOG_E(TAG, "Failed to swap XY ");
        return false;
    }

    if (esp_lcd_panel_mirror(panelHandle, configuration->mirrorX, configuration->mirrorY) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel to mirror");
        return false;
    }

    if (esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel to invert");
        return false;
    }

    if (esp_lcd_panel_disp_on_off(panelHandle, true) != ESP_OK) {
        LOG_E(TAG, "Failed to turn display on");
        return false;
    }

    return true;
}


lvgl_port_display_cfg_t Ili9488Display::getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
    return {
        .io_handle = ioHandle,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = configuration->bufferSize,
        .double_buffer = false,
        .trans_size = 0,
        .hres = configuration->horizontalResolution,
        .vres = configuration->verticalResolution,
        .monochrome = false,
        .rotation = {
            .swap_xy = configuration->swapXY,
            .mirror_x = configuration->mirrorX,
            .mirror_y = configuration->mirrorY,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false
        }
    };
}
