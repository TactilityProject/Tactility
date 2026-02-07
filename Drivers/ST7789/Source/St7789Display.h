#pragma once

#include <EspLcdSpiDisplay.h>

#include <driver/gpio.h>
#include <functional>
#include <lvgl.h>

class St7789Display final : public EspLcdSpiDisplay {

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
        uint32_t bufferSize; // Pixel count, not byte count. Set to 0 for default (1/10th of display size)
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        std::function<void(uint8_t)> _Nullable backlightDutyFunction;
        gpio_num_t resetPin;
        bool lvglSwapBytes;
        lcd_rgb_element_order_t rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB;
    };

private:

    static std::shared_ptr<EspLcdConfiguration> createEspLcdConfiguration(const Configuration& configuration);

    esp_lcd_panel_dev_config_t createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) override;

public:

    explicit St7789Display(const Configuration& configuration, const std::shared_ptr<SpiConfiguration>& spiConfiguration) :
        EspLcdSpiDisplay(
            createEspLcdConfiguration(configuration),
            spiConfiguration,
            4
        )
    {
    }

    std::string getName() const override { return "ST7789"; }

    std::string getDescription() const override { return "ST7789 display"; }
};

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
