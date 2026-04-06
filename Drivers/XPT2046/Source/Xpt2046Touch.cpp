#include "Xpt2046Touch.h"

#include <Tactility/settings/TouchCalibrationSettings.h>
#include <Tactility/lvgl/LvglSync.h>

#include <algorithm>
#include <esp_err.h>
#include <esp_lcd_touch_xpt2046.h>

static void processCoordinates(esp_lcd_touch_handle_t tp, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) {
    (void)strength;
    if (tp == nullptr || x == nullptr || y == nullptr || pointCount == nullptr || *pointCount == 0) {
        return;
    }

    auto* config = static_cast<Xpt2046Touch::Configuration*>(tp->config.user_data);
    if (config == nullptr) {
        return;
    }

    const auto settings = tt::settings::touch::getActive();
    const auto points = std::min<uint8_t>(*pointCount, maxPointCount);
    for (uint8_t i = 0; i < points; i++) {
        tt::settings::touch::applyCalibration(settings, config->xMax, config->yMax, x[i], y[i]);
    }
}

bool Xpt2046Touch::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    const esp_lcd_panel_io_spi_config_t io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(configuration->spiPinCs);
    return esp_lcd_new_panel_io_spi(configuration->spiDevice, &io_config, &outHandle) == ESP_OK;
}

bool Xpt2046Touch::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& config, esp_lcd_touch_handle_t& panelHandle) {
    return esp_lcd_touch_new_spi_xpt2046(ioHandle, &config, &panelHandle) == ESP_OK;
}

esp_lcd_touch_config_t Xpt2046Touch::createEspLcdTouchConfig() {
   return {
        .x_max = configuration->xMax,
        .y_max = configuration->yMax,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = configuration->swapXy,
            .mirror_x = configuration->mirrorX,
            .mirror_y = configuration->mirrorY,
        },
        .process_coordinates = processCoordinates,
        .interrupt_callback = nullptr,
        .user_data = configuration.get(),
        .driver_data = nullptr
    };
}
