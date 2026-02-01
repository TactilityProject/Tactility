#pragma once

#include <EspLcdSpiDisplay.h>

#include <driver/gpio.h>
#include <functional>
#include <lvgl.h>

class Ili934xDisplay final : public EspLcdSpiDisplay {

    std::shared_ptr<tt::Lock> lock;

public:

    /** Minimal set of overrides for EspLcdConfiguration */
    struct Configuration {
        unsigned int horizontalResolution;
        unsigned int verticalResolution;
        int gapX;
        int gapY;
        bool swapXY;
        bool mirrorX;
        bool mirrorY;
        bool invertColor;
        bool swapBytes;
        uint32_t bufferSize; // Pixel count, not byte count. Set to 0 for default (1/10th of display size)
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        std::function<void(uint8_t)> backlightDutyFunction; // Nullable
        gpio_num_t resetPin;
        lcd_rgb_element_order_t rgbElementOrder;
    };

private:

    static std::shared_ptr<EspLcdConfiguration> createEspLcdConfiguration(const Configuration& configuration);

    esp_lcd_panel_dev_config_t createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) override;

public:

    explicit Ili934xDisplay(const Configuration& configuration, const std::shared_ptr<SpiConfiguration>& spiConfiguration, bool hasGammaCurves) :
        EspLcdSpiDisplay(
            createEspLcdConfiguration(configuration),
            spiConfiguration,
            (hasGammaCurves ? 4 : 0)
        )
    {
    }

    std::string getName() const override { return "ILI934x"; }

    std::string getDescription() const override { return "ILI934x display"; }
};
