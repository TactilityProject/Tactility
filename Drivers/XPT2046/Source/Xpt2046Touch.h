#pragma once

#include <Tactility/hal/touch/TouchDevice.h>

#include <EspLcdTouch.h>

class Xpt2046Touch : public EspLcdTouch {

public:

    class Configuration {
    public:

        Configuration(
            esp_lcd_spi_bus_handle_t spiDevice,
            gpio_num_t spiPinCs,
            uint16_t xMax,
            uint16_t yMax,
            bool swapXy = false,
            bool mirrorX = false,
            bool mirrorY = false
        ) : spiDevice(spiDevice),
            spiPinCs(spiPinCs),
            xMax(xMax),
            yMax(yMax),
            swapXy(swapXy),
            mirrorX(mirrorX),
            mirrorY(mirrorY)
        {}

        esp_lcd_spi_bus_handle_t spiDevice;
        gpio_num_t spiPinCs;
        uint16_t xMax;
        uint16_t yMax;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
    };

private:

    std::unique_ptr<Configuration> configuration;

    bool createIoHandle(esp_lcd_panel_io_handle_t& outHandle) override;

    bool createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& panelHandle) override;

    esp_lcd_touch_config_t createEspLcdTouchConfig() override;

public:

    explicit Xpt2046Touch(std::unique_ptr<Configuration> inConfiguration) : configuration(std::move(inConfiguration)) {
        assert(configuration != nullptr);
    }

    std::string getName() const final { return "XPT2046"; }

    std::string getDescription() const final { return "XPT2046 I2C touch driver"; }
};
