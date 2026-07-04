#include "St7796Display.h"

#include <tactility/log.h>

#include <esp_lcd_panel_dev.h>
#include <esp_lcd_st7796.h>
#include <esp_lvgl_port.h>

constexpr auto* TAG = "ST7796";

bool St7796Display::createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) {
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

    return esp_lcd_new_panel_io_spi(configuration->spiHostDevice, &panel_io_config, &ioHandle) == ESP_OK;
}

bool St7796Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) {
    static const st7796_lcd_init_cmd_t lcd_init_cmds[] = {
        {0x01, (uint8_t[]) {0x00}, 0, 120},
        {0x11, (uint8_t[]) {0x00}, 0, 120},
        {0xF0, (uint8_t[]) {0xC3}, 1, 0},
        {0xF0, (uint8_t[]) {0xC3}, 1, 0},
        {0xF0, (uint8_t[]) {0x96}, 1, 0},
        {0x36, (uint8_t[]) {0x58}, 1, 0},
        {0x3A, (uint8_t[]) {0x55}, 1, 0},
        {0xB4, (uint8_t[]) {0x01}, 1, 0},
        {0xB6, (uint8_t[]) {0x80, 0x02, 0x3B}, 3, 0},
        {0xE8, (uint8_t[]) {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
        {0xC1, (uint8_t[]) {0x06}, 1, 0},
        {0xC2, (uint8_t[]) {0xA7}, 1, 0},
        {0xC5, (uint8_t[]) {0x18}, 1, 0},
        {0xE0, (uint8_t[]) {0xF0, 0x09, 0x0b, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 15, 0},
        {0xE1, (uint8_t[]) {0xE0, 0x09, 0x0b, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 15, 120},
        {0xF0, (uint8_t[]) {0x3C}, 1, 0},
        {0xF0, (uint8_t[]) {0x69}, 1, 0},
        {0x21, (uint8_t[]) {0x00}, 1, 0},
        {0x29, (uint8_t[]) {0x00}, 1, 0},
    };

    st7796_vendor_config_t vendor_config = {
        // Uncomment these lines if use custom initialization commands
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(st7796_lcd_init_cmd_t),
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration->resetPin, // Set to -1 if not use
        .color_space = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config
    };

    if (esp_lcd_new_panel_st7796(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
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

    if (esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel to invert");
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

    if (esp_lcd_panel_set_gap(panelHandle, configuration->gapX, configuration->gapY) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel gap");
        return false;
    }

    if (esp_lcd_panel_disp_on_off(panelHandle, true) != ESP_OK) {
        LOG_E(TAG, "Failed to turn display on");
        return false;
    }

    return true;
}

lvgl_port_display_cfg_t St7796Display::getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
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
        .color_format = LV_COLOR_FORMAT_NATIVE,
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

void St7796Display::setGammaCurve(uint8_t index) {
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

    /*if (esp_lcd_panel_io_tx_param(ioHandle , LCD_CMD_GAMSET, param, 1) != ESP_OK) {
        LOG_E(TAG, "Failed to set gamma");
    }*/
}
