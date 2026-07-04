#include "Jd9853Display.h"

#include <esp_lcd_jd9853.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lvgl_port.h>
#include <tactility/log.h>

constexpr auto* TAG = "JD9853";

bool Jd9853Display::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
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

bool Jd9853Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) {
    jd9853_vendor_config_t vendor_config = {
        .init_cmds = nullptr,
        .init_cmds_size = 0
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration->resetPin,
        .rgb_ele_order = configuration->rgbElementOrder,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = &vendor_config
    };

    if (esp_lcd_new_panel_jd9853(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
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
        LOG_E(TAG, "Failed to swap XY");
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

    if (esp_lcd_panel_set_gap(panelHandle, 34, 0) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel gap");
        return false;
    }

    return true;
}

lvgl_port_display_cfg_t Jd9853Display::getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
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
            .swap_bytes = true,
            .full_refresh = false,
            .direct_mode = false
        }
    };
}

/**
 * Note:
 * The datasheet implies this should work, but it doesn't:
 * https://www.digikey.com/htmldatasheets/production/1640716/0/0/1/ILI9341-Datasheet.pdf
 *
 * This repo claims it only has 1 curve:
 * https://github.com/brucemack/hello-ili9341
 *
 * I'm leaving it in as I'm not sure if it's just my hardware that's problematic.
 */
void Jd9853Display::setGammaCurve(uint8_t index) {
    uint8_t gamma_curve;
    switch (index) {
        case 0:
            gamma_curve = 0x01;
            break;
        case 1:
            gamma_curve = 0x04;
            break;
        case 2:
            gamma_curve = 0x02;
            break;
        case 3:
            gamma_curve = 0x08;
            break;
        default:
            return;
    }
    const uint8_t param[] = {
        gamma_curve
    };

    if (esp_lcd_panel_io_tx_param(getIoHandle() , LCD_CMD_GAMSET, param, 1) != ESP_OK) {
        LOG_E(TAG, "Failed to set gamma");
    }
}
