#include "EspLcdSpiDisplay.h"

#include <esp_lcd_panel_commands.h>
#include <tactility/log.h>

constexpr auto* TAG = "EspLcdSpiDisplay";

bool EspLcdSpiDisplay::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    LOG_I(TAG, "createIoHandle");

    const esp_lcd_panel_io_spi_config_t panel_io_config = {
        .cs_gpio_num = spiConfiguration->csPin,
        .dc_gpio_num = spiConfiguration->dcPin,
        .spi_mode = 0,
        .pclk_hz = spiConfiguration->pixelClockFrequency,
        .trans_queue_depth = spiConfiguration->transactionQueueDepth,
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
            .sio_mode = 1,
            .lsb_first = 0,
            .cs_high_active = 0
        }
    };

    if (esp_lcd_new_panel_io_spi(spiConfiguration->spiHostDevice, &panel_io_config, &outHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        return false;
    }

    return true;
}

void EspLcdSpiDisplay::setGammaCurve(uint8_t index) {
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

    auto io_handle = getIoHandle();
    assert(io_handle != nullptr);
    if (esp_lcd_panel_io_tx_param(io_handle, LCD_CMD_GAMSET, param, 1) != ESP_OK) {
        LOG_E(TAG, "Failed to set gamma");
    }
}
