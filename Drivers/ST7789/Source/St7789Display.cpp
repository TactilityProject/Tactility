#include "St7789Display.h"

#include <esp_lcd_panel_st7789.h>

std::shared_ptr<EspLcdConfiguration> St7789Display::createEspLcdConfiguration(const Configuration& configuration) {
    return std::make_shared<EspLcdConfiguration>(EspLcdConfiguration {
        .horizontalResolution = configuration.horizontalResolution,
        .verticalResolution = configuration.verticalResolution,
        .gapX = configuration.gapX,
        .gapY = configuration.gapY,
        .monochrome = false,
        .swapXY = configuration.swapXY,
        .mirrorX = configuration.mirrorX,
        .mirrorY = configuration.mirrorY,
        .invertColor = configuration.invertColor,
        .bufferSize = configuration.bufferSize,
        .buffSpiram = configuration.buffSpiram,
        .touch = configuration.touch,
        .backlightDutyFunction = configuration.backlightDutyFunction,
        .resetPin = configuration.resetPin,
        .lvglColorFormat = LV_COLOR_FORMAT_RGB565,
        .lvglSwapBytes = configuration.lvglSwapBytes,
        .rgbElementOrder = configuration.rgbElementOrder,
        .bitsPerPixel = 16,
    });
}

esp_lcd_panel_dev_config_t St7789Display::createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) {
    return {
        .reset_gpio_num = resetPin,
        .rgb_ele_order = espLcdConfiguration->rgbElementOrder,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = espLcdConfiguration->bitsPerPixel,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = nullptr
    };
}

bool St7789Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) {
    return esp_lcd_new_panel_st7789(ioHandle, &panelConfig, &panelHandle) == ESP_OK;
}
